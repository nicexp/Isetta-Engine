/*
 * Copyright (c) 2018 Isetta
 */

#include "Networking/NetworkingModule.h"

#include <list>
#include <utility>

#include "Core/Config/Config.h"
#include "Core/Debug/Logger.h"
#include "Core/IsettaAlias.h"
#include "Networking/BuiltinMessages.h"
#include "Networking/NetworkManager.h"
#include "brofiler/ProfilerCore/Brofiler.h"

// F Windows
#ifdef SendMessage
#undef SendMessage
#endif

namespace Isetta {

// Defining static variables
CustomAdapter NetworkingModule::NetworkAdapter;

void NetworkingModule::StartUp() {
  NetworkManager::Instance().networkingModule = this;
  networkManager = &NetworkManager::Instance();
  if (!InitializeYojimbo()) {
    throw std::exception(
        "NetworkingModule::StartUp => Could not initialize yojimbo.");
  }
  srand(static_cast<unsigned int>(time(nullptr)));

  // TODO(Caleb): Figure out some more robust channel settings
  yojimboConfig.numChannels = 2;
  yojimboConfig.channel[0].type = yojimbo::CHANNEL_TYPE_UNRELIABLE_UNORDERED;
  yojimboConfig.channel[1].type = yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED;
  yojimboConfig.timeout = CONFIG_VAL(networkConfig.timeout);

  privateKey = new (MemoryManager::AllocOnStack(
      sizeof(U8) * CONFIG_VAL(networkConfig.keyBytes)))
      U8[CONFIG_VAL(networkConfig.keyBytes)];
  // TODO(Caleb): Need to do something more insightful with the private key
  // than all 0s
  memset(privateKey, 0, CONFIG_VAL(networkConfig.keyBytes));

  clientId = 0;
  yojimbo::random_bytes(reinterpret_cast<U8*>(&clientId), 8);

  void* memPointer =
      MemoryManager::AllocOnStack(yojimboConfig.clientMemory + 1_MB);
  clientAllocator = new (MemoryManager::AllocOnStack(sizeof(NetworkAllocator)))
      NetworkAllocator(memPointer,
                       static_cast<Size>(yojimboConfig.clientMemory) + 1_MB);

  if (CONFIG_VAL(networkConfig.runServer)) {
    Size serverMemorySize = (yojimboConfig.serverPerClientMemory +
                             yojimboConfig.serverGlobalMemory) *
                            (CONFIG_VAL(networkConfig.maxClients) + 1);

    memPointer = MemoryManager::AllocOnStack(serverMemorySize);
    serverAllocator =
        new (MemoryManager::AllocOnStack(sizeof(NetworkAllocator)))
            NetworkAllocator(memPointer, serverMemorySize);
  }

  client = new (MemoryManager::AllocOnStack(sizeof(yojimbo::Client)))
      yojimbo::Client(
          clientAllocator,
          yojimbo::Address(CONFIG_VAL(networkConfig.defaultClientIP).c_str(),
                           CONFIG_VAL(networkConfig.clientPort)),
          yojimboConfig, NetworkAdapter, clock.GetElapsedTime());

  clientSendBuffer =
      MemoryManager::NewArrOnFreeList<RingBuffer<yojimbo::Message*>>(
          CONFIG_VAL(networkConfig.clientQueueSize));

  RegisterBuiltinCallbacks();
}

void NetworkingModule::Update(float deltaTime) {
  BROFILER_CATEGORY("Network Update", Profiler::Color::Orange);

  clock.UpdateTime();

  // Check for new connections
  PumpClientServerUpdate(clock.GetElapsedTime());

  // Send out our messages
  SendClientToServerMessages();
  if (server) {
    for (int i = 0; i < CONFIG_VAL(networkConfig.maxClients); ++i) {
      SendServerToClientMessages(i);
    }
  }

  // Receive and process the messages from the other side
  PumpClientServerUpdate(clock.GetElapsedTime());

  if (client->IsConnected()) {
    ProcessServerToClientMessages();
  }

  if (server) {
    for (int i = 0; i < CONFIG_VAL(networkConfig.maxClients); ++i) {
      ProcessClientToServerMessages(i);
    }
  }
}

void NetworkingModule::ShutDown() {
  try {
    Disconnect();
  } catch (std::exception& e) {
  }

  try {
    CloseServer();
  } catch (std::exception& e) {
  }

  ShutdownYojimbo();

  client->~Client();
  MemoryManager::DeleteArrOnFreeList<RingBuffer<yojimbo::Message*>>(
      CONFIG_VAL(networkConfig.clientQueueSize), clientSendBuffer);
  clientAllocator->~NetworkAllocator();
}

// NOTE: Deletes the oldest message in the queue if the queue is
// overflowing--it would be nice to return it, but then we wouldn't
// be able to guarantee the memory is reused
void NetworkingModule::AddClientToServerMessage(
    yojimbo::Message* message) const {
  if (!IsClientRunning()) {
    LOG_ERROR(Debug::Channel::Networking,
              "Cannot send message from client cause client is not running");
    return;
  }
  if (clientSendBuffer->IsFull()) {
    client->ReleaseMessage(
        clientSendBuffer->Get());  // This may be horribly wrong
  }
  clientSendBuffer->Put(message);
}

// NOTE: Deletes the oldest message in the queue if the queue is
// overflowing--it would be nice to return it, but then we wouldn't
// be able to guarantee the memory is reused
void NetworkingModule::AddServerToClientMessage(int clientIdx,
                                                yojimbo::Message* message) {
  if (!IsServerRunning()) {
    LOG_ERROR(Debug::Channel::Networking,
              "Cannot send message from server cause server is not running");
    return;
  }
  if (serverSendBufferArray[clientIdx].IsFull()) {
    server->ReleaseMessage(
        clientIdx,
        serverSendBufferArray[clientIdx]
            .Get());  // TODO(Caleb): This may be horribly wrong; test that
                      // ReleaseMessage works from source
  }
  serverSendBufferArray[clientIdx].Put(message);
}

void NetworkingModule::PumpClientServerUpdate(double time) {
  PROFILE
  client->SendPackets();
  if (server) {
    server->SendPackets();
  }

  client->ReceivePackets();
  if (server) {
    server->ReceivePackets();
  }

  client->AdvanceTime(time);
  if (server) {
    server->AdvanceTime(time);
  }
}

void NetworkingModule::SendClientToServerMessages() const {
  const int channelIdx = 0;  // TODO(Caleb): Upgrade the channel indexing
  while (!clientSendBuffer->IsEmpty()) {
    if (!client->CanSendMessage(channelIdx)) {
      break;
    }

    yojimbo::Message* message = clientSendBuffer->Get();
    client->SendMessage(channelIdx, message);  // bugged out
  }
}

void NetworkingModule::SendServerToClientMessages(int clientIdx) const {
  const int channelIdx = 0;  // TODO(Caleb): Upgrade the channel indexing
  while (!serverSendBufferArray[clientIdx].IsEmpty()) {
    if (!server->CanSendMessage(clientIdx, channelIdx)) {
      break;
    }

    yojimbo::Message* message = serverSendBufferArray[clientIdx].Get();
    server->SendMessage(clientIdx, channelIdx, message);
  }
}

void NetworkingModule::ProcessClientToServerMessages(int clientIdx) const {
  const int channelIdx = 0;  // TODO(Caleb): Upgrade the channel indexing
  for (;;) {
    yojimbo::Message* message = server->ReceiveMessage(clientIdx, channelIdx);

    if (!message) {
      break;
    }

    auto serverFunctions =
        NetworkManager::Instance().GetServerFunctions(message->GetType());
    for (const auto& function : serverFunctions) {
      function.second(clientIdx, message);
    }

    server->ReleaseMessage(clientIdx, message);
  }
}

void NetworkingModule::ProcessServerToClientMessages() const {
  const int channelIdx = 0;  // TODO(Caleb): Upgrade the channel indexing
  for (;;) {
    yojimbo::Message* message = client->ReceiveMessage(channelIdx);

    if (!message) {
      break;
    }

    auto clientFunctions =
        NetworkManager::Instance().GetClientFunctions(message->GetType());
    for (const auto& function : clientFunctions) {
      function.second(message);
    }

    client->ReleaseMessage(message);
  }
}

void NetworkingModule::Connect(const char* serverAddress, int serverPort,
                               Action<bool> callback) {
  if (IsClientRunning()) {
    LOG_ERROR(Debug::Channel::Networking,
              "Already running as client. Cannot StartClient again");
    return;
  }

  yojimbo::Address address(serverAddress, serverPort);
  if (!address.IsValid()) {
    if (IsClientRunning()) {
      LOG_ERROR(Debug::Channel::Networking,
                "IP Address for StartClient is invalid");
      return;
    }
    return;
  }
  Action<bool> internalCallback = [=](const bool success) {
    if (callback != nullptr) {
      callback(success);
    }
    NetworkManager::Instance().SendMessageFromClient(
        NetworkManager::Instance()
            .GenerateMessageFromClient<ClientConnectedMessage>());
  };
  client->InsecureConnect(privateKey, clientId, address, internalCallback);
  onConnectedToServer.Invoke();
}

void NetworkingModule::Disconnect() {
  if (client->IsConnected()) {
    client->Disconnect();
    onDisconnectedFromServer.Invoke();
  } else if (!client->IsConnecting()) {
    return;
  } else {
    throw std::exception(
        "NetworkingModule::Disconnect => Cannot disconnect the client if it is "
        "not already connected.");
  }
}

void NetworkingModule::CreateServer(const char* address, int port) {
  if (IsServerRunning()) {
    LOG_ERROR(
        Debug::Channel::Networking,
        "NetworkingModule::CreateServer => Cannot create a server while one is "
        "already running.");
  }

  serverSendBufferArray =
      MemoryManager::NewArrOnFreeList<RingBuffer<yojimbo::Message*>>(
          CONFIG_VAL(networkConfig.maxClients));
  for (int i = 0; i < CONFIG_VAL(networkConfig.maxClients); ++i) {
    serverSendBufferArray[i] = RingBuffer<yojimbo::Message*>(
        CONFIG_VAL(networkConfig.serverQueueSizePerClient));
  }

  serverAddress = yojimbo::Address(address, port);
  server = MemoryManager::NewOnFreeList<yojimbo::Server>(
      serverAllocator, privateKey, serverAddress, yojimboConfig,
      &NetworkingModule::NetworkAdapter, clock.GetElapsedTime());
  server->Start(CONFIG_VAL(networkConfig.maxClients));

  if (!server->IsRunning()) {
    throw std::exception(
        "NetworkingModule::CreateServer => Unable to run server.");
  }
}

void NetworkingModule::CloseServer() {
  if (!IsServerRunning()) {
    throw std::exception(
        "NetworkingModule::CloseServer() Cannot close the server if it is not "
        "running.");
  }

  server->Stop();
  MemoryManager::DeleteOnFreeList<yojimbo::Server>(server);
  server = nullptr;
  MemoryManager::DeleteArrOnFreeList<RingBuffer<yojimbo::Message*>>(
      CONFIG_VAL(networkConfig.maxClients), serverSendBufferArray);
  serverAllocator->~NetworkAllocator();
}

bool NetworkingModule::IsClient() const {
  return client->IsConnected() && server == nullptr && !server->IsRunning();
}

bool NetworkingModule::IsHost() const {
  return client->IsConnected() && server != nullptr && server->IsRunning();
}

bool NetworkingModule::IsServer() const {
  return !client->IsConnected() && server != nullptr && server->IsRunning();
}

bool NetworkingModule::IsClientRunning() const {
  return client != nullptr && client->IsConnected();
}

bool NetworkingModule::IsServerRunning() const {
  return server != nullptr && server->IsRunning();
}

bool NetworkingModule::IsClientConnected(const int clientIndex) const {
  return IsServerRunning() && server->IsClientConnected(clientIndex);
}

void NetworkingModule::RegisterBuiltinCallbacks() {
  NetworkManager::Instance().RegisterServerCallback<ClientConnectedMessage>(
      [this](const int clientIndex, yojimbo::Message*) {
        this->onClientConnected.Invoke(clientIndex);
      });
}

}  // namespace Isetta

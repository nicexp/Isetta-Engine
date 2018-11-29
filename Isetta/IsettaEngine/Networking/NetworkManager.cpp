/*
 * Copyright (c) 2018 Isetta
 */

#include "Networking/NetworkManager.h"
#include "Core/Config/Config.h"
#include "Networking/NetworkId.h"
#include "Networking/NetworkTransform.h"
#include "Networking/NetworkingModule.h"
#include "Scene/Entity.h"

namespace Isetta {

NetworkManager::NetworkManager() : networkIds(1) {}

NetworkManager& NetworkManager::Instance() {
  static NetworkManager instance;
  return instance;
}

void NetworkManager::SendMessageFromClient(yojimbo::Message* message) const {
  networkingModule->AddClientToServerMessage(message);
}

void NetworkManager::SendMessageFromServer(const int clientIdx,
                                           yojimbo::Message* message) const {
  networkingModule->AddServerToClientMessage(clientIdx, message);
}

bool NetworkManager::LocalClientIsConnected() const {
  return networkingModule->client->IsConnected();
}

bool NetworkManager::ClientIsConnected(const int clientIdx) const {
  return networkingModule->server->IsClientConnected(clientIdx);
}

bool NetworkManager::ServerIsRunning() const {
  return networkingModule->server && networkingModule->server->IsRunning();
}

int NetworkManager::GetMaxClients() {
  return CONFIG_VAL(networkConfig.maxClients);
}

int NetworkManager::GetClientIndex() const {
  return networkingModule->client->GetClientIndex();
}

void NetworkManager::StartServer(const char* serverIP) const {
  networkingModule->CreateServer(serverIP, GetServerPort());
}

void NetworkManager::StartServer(const std::string& serverIP) const {
  StartServer(serverIP.c_str());
}

void NetworkManager::StopServer() const { networkingModule->CloseServer(); }

void NetworkManager::StartClient(const char* serverIP,
                                 const Action<bool>& onStarted) const {
  networkingModule->Connect(serverIP, GetServerPort(), onStarted);
}

void NetworkManager::StartClient(const std::string& serverIP,
                                 const Action<bool>& onStarted) const {
  StartClient(serverIP.c_str(), onStarted);
}

void NetworkManager::StopClient() const { networkingModule->Disconnect(); }

void NetworkManager::StartHost(const char* hostIP) const {
  networkingModule->CreateServer(hostIP, GetServerPort());
  networkingModule->Connect(hostIP, GetServerPort());
  LOG_INFO(Debug::Channel::Networking, "Host Started on %s", hostIP);
}

void NetworkManager::StartHost(const std::string& hostIP) const {
  StartHost(hostIP.c_str());
}

void NetworkManager::StopHost() const {
  networkingModule->Disconnect();
  networkingModule->CloseServer();
}

std::list<std::pair<U16, Action<yojimbo::Message*>>>
NetworkManager::GetClientFunctions(const int type) {
  return clientCallbacks[type];
}
std::list<std::pair<U16, Action<int, yojimbo::Message*>>>
NetworkManager::GetServerFunctions(const int type) {
  return serverCallbacks[type];
}

yojimbo::Message* NetworkManager::CreateClientMessage(
    const int messageId) const {
  return networkingModule->client->CreateMessage(messageId);
}

yojimbo::Message* NetworkManager::CreateServerMessage(
    const int clientIdx, const int messageId) const {
  return networkingModule->server->CreateMessage(clientIdx, messageId);
}

Entity* NetworkManager::GetNetworkEntity(const U32 id) {
  auto it = networkIdToComponentMap.find(id);
  if (it != networkIdToComponentMap.end()) {
    return it->second->entity;
  }
  return nullptr;
}

NetworkId* NetworkManager::GetNetworkId(const U32 id) {
  auto it = networkIdToComponentMap.find(id);
  if (it != networkIdToComponentMap.end()) {
    return it->second;
  }
  return nullptr;
}

U32 NetworkManager::CreateNetworkId(NetworkId* networkId) {
  if (!ServerIsRunning()) {
    throw std::exception("Cannot create a new network id on a client");
  }
  U32 netId = networkIds.GetHandle();
  networkId->id = netId;
  networkIdToComponentMap[netId] = networkId;
  NetworkTransform::serverPosTimestamps[netId] = 0;
  NetworkTransform::serverRotTimestamps[netId] = 0;
  NetworkTransform::serverScaleTimestamps[netId] = 0;
}

U32 NetworkManager::AssignNetworkId(U32 netId, NetworkId* networkId) {
  if (networkIdToComponentMap.find(netId) != networkIdToComponentMap.end()) {
    throw std::exception(Util::StrFormat(
        "Multiple objects trying to assign to the same network id: %d", netId));
  } else if (networkId->id > 0) {
    throw std::exception(
        Util::StrFormat("Trying to assign network id %d to existing network "
                        "object with id %d",
                        netId, networkId->id));
  }
  networkId->id = netId;
  networkIdToComponentMap[netId] = networkId;
  networkIds.RemoveHandle(netId);
}

void NetworkManager::RemoveNetworkId(NetworkId* networkId) {
  if (!networkId->id) {
    throw std::exception(Util::StrFormat(
        "Cannot remove network id on a nonexistent network object"));
  }
  networkIdToComponentMap.erase(networkId->id);
  networkIds.ReturnHandle(networkId->id);
  networkId->id = NULL;
}

inline U16 NetworkManager::GetServerPort() {
  return CONFIG_VAL(networkConfig.serverPort);
}
}  // namespace Isetta

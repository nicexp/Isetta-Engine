/*
 * Copyright (c) 2018 Isetta
 */
#include "BVHLevel.h"

#include "Components/Editor/FrameReporter.h"
#include "Components/FlyController.h"
#include "Components/GridComponent.h"

#include "BVHLevel/RandomMover.h"
#include "Components/Editor/EditorComponent.h"
#include "Custom/DebugCollision.h"
#include "Custom/RaycastClick.h"
#include "Custom/EscapeExit.h"

namespace Isetta {

void BVHLevel::Load() {
  Entity* lightEntity = Entity::Instantiate("Light");
  lightEntity->AddComponent<LightComponent>();
  lightEntity->SetTransform(Math::Vector3{0, 200, 600}, Math::Vector3::zero,
                            Math::Vector3::one);

  Entity* cameraEntity = Entity::Instantiate("Camera");
  cameraEntity->AddComponent<CameraComponent>();
  cameraEntity->SetTransform(Math::Vector3{0, 5, 10}, Math::Vector3{-15, 0, 0},
                             Math::Vector3::one);
  cameraEntity->AddComponent<FlyController>();

  Entity* debug = Entity::Instantiate("Debug");
  debug->AddComponent<GridComponent>();
  debug->AddComponent<EditorComponent>();
  debug->AddComponent<EscapeExit>();
  debug->AddComponent<FrameReporter>();
  debug->AddComponent<RaycastClick>(true);

  Config::Instance().drawConfig.bvtDrawAABBs.SetVal("1");

  static bool enable = true;
  Input::RegisterKeyPressCallback(KeyCode::SPACE, [&]() {
    enable = !enable;
    for (RandomMover* randomMover : randomMovers) {
      randomMover->SetActive(enable);
    }
  });

  Input::RegisterKeyPressCallback(KeyCode::KP_ENTER, [&]() {
    Config::Instance().drawConfig.bvtDrawAABBs.SetVal(
        Config::Instance().drawConfig.bvtDrawAABBs.GetVal() ? "0" : "1");
  });

  static float range = 10;
  Input::RegisterKeyPressCallback(KeyCode::KP_7, [&]() {
    --range;
    for (RandomMover* randomMover : randomMovers) {
      randomMover->range = range;
      randomMover->coolDown = 0;
    }
  });
  Input::RegisterKeyPressCallback(KeyCode::KP_9, [&]() {
    ++range;
    for (RandomMover* randomMover : randomMovers) {
      randomMover->range = range;
      randomMover->coolDown = 0;
    }
  });

  Input::RegisterKeyPressCallback(KeyCode::KP_5, [&]() {
    for (int i = 0; i < 100; ++i) {
      ++count;
      Entity* sphere{
          Entity::Instantiate(Util::StrFormat("Sphere (%d)", count))};
      randomMovers.PushBack(sphere->AddComponent<RandomMover>());
      randomMovers.Back()->SetActive(enable);
      randomMovers.Back()->range = range;
      sphere->AddComponent<SphereCollider>();
      sphere->AddComponent<CollisionHandler>();
      sphere->AddComponent<DebugCollision>();
      const float size = 20;
      sphere->SetTransform(size *
                           Math::Vector3{Math::Random::GetRandom01() - 0.5f,
                                         Math::Random::GetRandom01(),
                                         Math::Random::GetRandom01() - 0.5f});
    }
  });

  Input::RegisterKeyPressCallback(KeyCode::KP_6, [&]() {
    ++count;
    Entity* sphere{Entity::Instantiate(Util::StrFormat("Sphere (%d)", count))};
    randomMovers.PushBack(sphere->AddComponent<RandomMover>());
    randomMovers.Back()->SetActive(enable);
    randomMovers.Back()->range = range;
    sphere->AddComponent<SphereCollider>();
    sphere->AddComponent<CollisionHandler>();
    sphere->AddComponent<DebugCollision>();
    const float size = 20;
    sphere->SetTransform(size *
                         Math::Vector3{Math::Random::GetRandom01() - 0.5f,
                                       Math::Random::GetRandom01(),
                                       Math::Random::GetRandom01() - 0.5f});
    spheres.push(sphere);
  });

  Input::RegisterKeyPressCallback(KeyCode::KP_4, [&]() {
    if (!spheres.empty()) {
      count--;
      Entity* sphere = spheres.front();
      spheres.pop();
      Entity::Destroy(sphere);
    }
  });
}
}  // namespace Isetta
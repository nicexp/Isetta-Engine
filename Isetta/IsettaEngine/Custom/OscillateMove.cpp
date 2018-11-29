/*
 * Copyright (c) 2018 Isetta
 */
#include "Custom/OscillateMove.h"

#include "Core/Debug/DebugDraw.h"
#include "Core/Math/Util.h"
#include "Core/Time/Clock.h"
#include "EngineLoop.h"
#include "Scene/Transform.h"

namespace Isetta {
void OscillateMove::OnEnable() {
  if (axis < 0 || axis > Math::Vector3::ELEMENT_COUNT - 1) {
    this->SetActive(false);
  }
}
void OscillateMove::Update() {
  float dist = Math::Vector3::Dot(transform->GetLocalPos() - origin,
                                  transform->GetAxis(axis));
  if (distance < dist) {
    direction = -1;
  } else if (-distance > dist) {
    direction = 1;
  }
  transform->TranslateLocal(direction * transform->GetAxis(axis) *
                                speed *
                                EngineLoop::GetGameClock().GetDeltaTime());
  // DebugDraw::Cube(Math::Matrix4::Translate(transform->GetWorldPos()));
}
}  // namespace Isetta
/*
 * Copyright (c) 2018 Isetta
 */
#pragma once
#include "Scene/Component.h"
#include "Core/IsettaAlias.h"
#include <queue>

namespace Isetta {
BEGIN_COMPONENT(FrameReporter, Component, false)
public:
void GuiUpdate() override;

private:
int reportInterval{20};
int count{0};
float fps{};
float frameTime{};

float timeSumForAvg{0.f};
Size frameCountForAvg{60};
std::queue<float> frameDurations;

END_COMPONENT(FrameReporter, Component)
}  // namespace Isetta
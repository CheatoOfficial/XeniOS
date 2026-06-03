/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/touch/touch_layout_ios_internal.h"

#include <utility>

namespace xe {
namespace hid {
namespace touch {

namespace {

constexpr IOSTouchRect kMoveStickFrame = {0.01f, 0.55f, 0.28f, 0.35f};
constexpr IOSTouchRect kLookZoneFrame = {0.20f, 0.06f, 0.78f, 0.86f};
constexpr IOSTouchRect kPauseFrame = {0.015f, 0.02f, 0.09f, 0.085f};
constexpr IOSTouchRect kAimFrame = {0.60f, 0.67f, 0.13f, 0.14f};
constexpr IOSTouchRect kReloadFrame = {0.72f, 0.47f, 0.13f, 0.14f};
constexpr IOSTouchRect kJumpFrame = {0.86f, 0.54f, 0.13f, 0.14f};
constexpr IOSTouchRect kFireFrame = {0.75f, 0.74f, 0.23f, 0.17f};

IOSTouchControlDefinition MakeActionButton(const char* identifier,
                                           IOSTouchAction action,
                                           const IOSTouchRect& normalized_frame,
                                           uint8_t capture_priority) {
  IOSTouchControlDefinition control;
  control.identifier = identifier;
  control.type = IOSTouchControlType::kActionButton;
  control.shape = IOSTouchControlShape::kCircle;
  control.normalized_frame = normalized_frame;
  control.activation_radius = 0.5f;
  control.visual_opacity = 0.92f;
  control.capture_priority = capture_priority;
  ConfigureIOSTouchControlAction(action, &control);
  return control;
}

}  // namespace

IOSTouchControlDefinition MakeDefaultIOSTouchControlDefinitionImpl(
    IOSTouchControlType type) {
  switch (type) {
    case IOSTouchControlType::kMoveStick: {
      IOSTouchControlDefinition control;
      control.identifier = "move_stick";
      control.type = IOSTouchControlType::kMoveStick;
      control.action = IOSTouchAction::kMove;
      control.shape = IOSTouchControlShape::kCircle;
      control.normalized_frame = kMoveStickFrame;
      control.deadzone = 0.14f;
      control.activation_radius = 0.48f;
      control.visual_opacity = 0.80f;
      control.capture_priority = 200;
      return control;
    }
    case IOSTouchControlType::kLookSwipeZone: {
      IOSTouchControlDefinition control;
      control.identifier = "look_zone";
      control.type = IOSTouchControlType::kLookSwipeZone;
      control.action = IOSTouchAction::kLook;
      control.shape = IOSTouchControlShape::kRoundedRect;
      control.normalized_frame = kLookZoneFrame;
      control.visual_opacity = 0.0f;
      control.capture_priority = 16;
      return control;
    }
    case IOSTouchControlType::kPauseButton: {
      IOSTouchControlDefinition control;
      control.identifier = "pause_button";
      control.type = IOSTouchControlType::kPauseButton;
      control.action = IOSTouchAction::kPauseMenu;
      control.shape = IOSTouchControlShape::kRoundedRect;
      control.normalized_frame = kPauseFrame;
      control.visual_opacity = 0.92f;
      control.capture_priority = 255;
      return control;
    }
    case IOSTouchControlType::kActionButton:
    default:
      return MakeActionButton("action_button", IOSTouchAction::kButtonA,
                              IOSTouchRect{0.82f, 0.32f, 0.12f, 0.14f}, 232);
  }
}

IOSTouchLayoutModel CreateDefaultIOSFPSLayoutModel() {
  IOSTouchLayoutModel layout;
  layout.layout_id = "fps_phase1_halo_like";
  layout.display_name = "FPS Phase 1";
  layout.author = "XeniOS";
  layout.base_template = "fps_standard";

  layout.controls.push_back(MakeDefaultIOSTouchControlDefinitionImpl(
      IOSTouchControlType::kMoveStick));
  layout.controls.push_back(MakeDefaultIOSTouchControlDefinitionImpl(
      IOSTouchControlType::kLookSwipeZone));
  layout.controls.push_back(MakeDefaultIOSTouchControlDefinitionImpl(
      IOSTouchControlType::kPauseButton));
  layout.controls.push_back(MakeActionButton(
      "aim_button", IOSTouchAction::kLeftTrigger, kAimFrame, 240));
  layout.controls.push_back(MakeActionButton(
      "reload_button", IOSTouchAction::kButtonX, kReloadFrame, 236));
  layout.controls.push_back(MakeActionButton(
      "jump_button", IOSTouchAction::kButtonA, kJumpFrame, 238));
  layout.controls.push_back(MakeActionButton(
      "fire_button", IOSTouchAction::kRightTrigger, kFireFrame, 242));

  return layout;
}

IOSTouchControlDefinition CreateDefaultIOSTouchControlDefinition(
    IOSTouchControlType type) {
  return MakeDefaultIOSTouchControlDefinitionImpl(type);
}

}  // namespace touch
}  // namespace hid
}  // namespace xe

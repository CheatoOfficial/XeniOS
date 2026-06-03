/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/touch/touch_input_resolver.h"

#include <algorithm>
#include <cmath>

#include "xenia/hid/input.h"

namespace xe::hid::touch {
namespace {

constexpr float kTouchAxisMax = 32767.0f;
constexpr float kTouchComboStickRadiusFraction = 0.32f;
constexpr float kMoveStickDoubleTapMaxSeconds = 0.34f;
constexpr float kMoveStickDoubleTapForwardThreshold = 0.58f;
constexpr float kMoveStickDoubleTapLateralThreshold = 0.58f;

}  // namespace

IOSTouchPoint ClampTouchLookVector(IOSTouchPoint value) {
  return IOSTouchPoint{std::clamp(value.x, -1.0f, 1.0f),
                       std::clamp(value.y, -1.0f, 1.0f)};
}

IOSTouchPoint TouchSwipeLookVectorForDelta(IOSTouchPoint delta,
                                           float look_scale,
                                           float points_per_full_scale,
                                           float vertical_scale) {
  const float safe_points_per_full_scale =
      std::clamp(points_per_full_scale, 1.0f, 64.0f);
  const float clamped_look_scale = std::clamp(look_scale, 0.25f, 4.0f);
  const float clamped_vertical_scale = std::clamp(vertical_scale, 0.25f, 4.0f);
  return ClampTouchLookVector(
      IOSTouchPoint{delta.x / safe_points_per_full_scale * clamped_look_scale,
                    -delta.y / safe_points_per_full_scale * clamped_look_scale *
                        clamped_vertical_scale});
}

IOSTouchComboSubzone ResolveTouchComboSubzone(
    const IOSTouchControlDefinition& control,
    const IOSTouchRect& resolved_frame, IOSTouchPoint point) {
  if (!control.move_with_dpad_ring ||
      control.type != IOSTouchControlType::kMoveStick) {
    return IOSTouchComboSubzone::kStick;
  }
  if (resolved_frame.width <= 0.0f || resolved_frame.height <= 0.0f) {
    return IOSTouchComboSubzone::kStick;
  }
  const float centre_x = resolved_frame.x + resolved_frame.width * 0.5f;
  const float centre_y = resolved_frame.y + resolved_frame.height * 0.5f;
  const float short_side =
      std::min(resolved_frame.width, resolved_frame.height);
  const float stick_radius = short_side * kTouchComboStickRadiusFraction;
  const float dx = point.x - centre_x;
  const float dy = point.y - centre_y;
  const float distance_squared = dx * dx + dy * dy;
  if (distance_squared <= stick_radius * stick_radius) {
    return IOSTouchComboSubzone::kStick;
  }
  if (std::abs(dx) > std::abs(dy)) {
    return dx > 0.0f ? IOSTouchComboSubzone::kDpadRight
                     : IOSTouchComboSubzone::kDpadLeft;
  }
  return dy > 0.0f ? IOSTouchComboSubzone::kDpadDown
                   : IOSTouchComboSubzone::kDpadUp;
}

bool TouchControlContainsPoint(const IOSTouchControlDefinition& control,
                               const IOSTouchRect& resolved_frame,
                               IOSTouchPoint point) {
  if (!IOSTouchRectContainsPoint(resolved_frame, point)) {
    return false;
  }
  if (control.move_with_dpad_ring &&
      control.type == IOSTouchControlType::kMoveStick) {
    return true;
  }
  if (control.shape != IOSTouchControlShape::kCircle) {
    return true;
  }

  const float width = resolved_frame.width;
  const float height = resolved_frame.height;
  if (width <= 0.0f || height <= 0.0f) {
    return false;
  }

  const float radius = std::min(width, height) * 0.5f;
  if (width >= height) {
    if (point.x >= resolved_frame.x + radius &&
        point.x <= resolved_frame.x + width - radius) {
      return true;
    }
    const float center_y = resolved_frame.y + height * 0.5f;
    const float left_center_x = resolved_frame.x + radius;
    const float right_center_x = resolved_frame.x + width - radius;
    const float left_dx = point.x - left_center_x;
    const float right_dx = point.x - right_center_x;
    const float dy = point.y - center_y;
    return left_dx * left_dx + dy * dy <= radius * radius ||
           right_dx * right_dx + dy * dy <= radius * radius;
  }

  if (point.y >= resolved_frame.y + radius &&
      point.y <= resolved_frame.y + height - radius) {
    return true;
  }
  const float center_x = resolved_frame.x + width * 0.5f;
  const float top_center_y = resolved_frame.y + radius;
  const float bottom_center_y = resolved_frame.y + height - radius;
  const float dx = point.x - center_x;
  const float top_dy = point.y - top_center_y;
  const float bottom_dy = point.y - bottom_center_y;
  return dx * dx + top_dy * top_dy <= radius * radius ||
         dx * dx + bottom_dy * bottom_dy <= radius * radius;
}

int16_t TouchAxisFromUnit(float unit_value) {
  const float clamped_value = std::clamp(unit_value, -1.0f, 1.0f);
  return static_cast<int16_t>(std::lround(clamped_value * kTouchAxisMax));
}

bool TouchStatesEqualIgnoringPacket(const IOSTouchResolvedState& left,
                                    const IOSTouchResolvedState& right) {
  return left.buttons == right.buttons &&
         left.left_trigger == right.left_trigger &&
         left.right_trigger == right.right_trigger &&
         left.thumb_lx == right.thumb_lx && left.thumb_ly == right.thumb_ly &&
         left.thumb_rx == right.thumb_rx && left.thumb_ry == right.thumb_ry &&
         left.gameplay_enabled == right.gameplay_enabled &&
         left.pause_requested == right.pause_requested;
}

void ApplyTouchActionMapping(const IOSTouchControlDefinition& control,
                             IOSTouchResolvedState* state) {
  if (!state) {
    return;
  }
  state->buttons |= control.mapped_buttons;
  state->left_trigger =
      std::max(state->left_trigger, control.mapped_left_trigger);
  state->right_trigger =
      std::max(state->right_trigger, control.mapped_right_trigger);
}

void ApplyTouchActionMappingForAction(IOSTouchAction action,
                                      IOSTouchResolvedState* state) {
  IOSTouchControlDefinition control;
  ConfigureIOSTouchControlAction(action, &control);
  ApplyTouchActionMapping(control, state);
}

bool TouchInteractionBehaviorConfigured(
    const IOSTouchInteractionBehavior& behavior) {
  return behavior.trigger != IOSTouchInteractionTrigger::kNone &&
         (behavior.action != IOSTouchAction::kNone ||
          behavior.enables_relative_look);
}

IOSTouchInteractionBehaviorState ResolveTouchInteractionBehaviorState(
    const IOSTouchInteractionBehavior& behavior,
    const IOSTouchInputCapture& capture, double current_time) {
  IOSTouchInteractionBehaviorState state;
  if (!TouchInteractionBehaviorConfigured(behavior) ||
      capture.began_time <= 0.0) {
    return state;
  }

  const float elapsed_seconds =
      static_cast<float>(current_time - capture.began_time);
  switch (behavior.trigger) {
    case IOSTouchInteractionTrigger::kHold: {
      const float hold_seconds = std::clamp(behavior.hold_seconds, 0.05f, 1.0f);
      if (elapsed_seconds < hold_seconds) {
        return state;
      }
    } break;
    case IOSTouchInteractionTrigger::kHoldDrag: {
      const float hold_seconds = std::clamp(behavior.hold_seconds, 0.05f, 1.0f);
      if (elapsed_seconds < hold_seconds) {
        return state;
      }
      const float drag_distance =
          std::hypot(capture.current_point.x - capture.anchor_point.x,
                     capture.current_point.y - capture.anchor_point.y);
      if (drag_distance <
          std::clamp(behavior.drag_threshold_points, 2.0f, 96.0f)) {
        return state;
      }
    } break;
    case IOSTouchInteractionTrigger::kDoubleTap:
    case IOSTouchInteractionTrigger::kDoubleTapForward:
    case IOSTouchInteractionTrigger::kNone:
    default:
      return state;
  }

  state.active = true;
  state.enables_relative_look = behavior.enables_relative_look;
  state.relative_look_scale =
      std::clamp(behavior.relative_look_scale, 0.1f, 2.0f);
  return state;
}

bool TouchControlUsesDeferredPrimaryTap(
    const IOSTouchControlDefinition& control) {
  return control.type == IOSTouchControlType::kActionButton &&
         !control.hold_while_captured &&
         (control.secondary_behavior.trigger ==
              IOSTouchInteractionTrigger::kHold ||
          control.secondary_behavior.trigger ==
              IOSTouchInteractionTrigger::kHoldDrag) &&
         TouchInteractionBehaviorConfigured(control.secondary_behavior);
}

IOSTouchPoint MoveStickUnitVectorForCapture(
    const IOSTouchControlDefinition& control, const IOSTouchRect& frame,
    const IOSTouchInputCapture& capture) {
  const float outer_radius = std::min(frame.width, frame.height) *
                             std::max(control.activation_radius, 0.24f);
  IOSTouchPoint delta{capture.current_point.x - capture.anchor_point.x,
                      capture.current_point.y - capture.anchor_point.y};
  const float distance = std::hypot(delta.x, delta.y);
  if (distance > outer_radius && distance > 0.0f) {
    const float scale = outer_radius / distance;
    delta.x *= scale;
    delta.y *= scale;
  }

  float normalized_x = outer_radius > 0.0f ? delta.x / outer_radius : 0.0f;
  float normalized_y = outer_radius > 0.0f ? delta.y / outer_radius : 0.0f;
  const float magnitude =
      std::sqrt(normalized_x * normalized_x + normalized_y * normalized_y);
  if (magnitude < control.deadzone || magnitude <= 0.0f) {
    return IOSTouchPoint{};
  }
  const float rescaled_magnitude = std::clamp(
      (magnitude - control.deadzone) / (1.0f - control.deadzone), 0.0f, 1.0f);
  normalized_x = normalized_x / magnitude * rescaled_magnitude;
  normalized_y = normalized_y / magnitude * rescaled_magnitude;
  return IOSTouchPoint{normalized_x, normalized_y};
}

bool MoveStickCaptureQualifiesForDoubleTapForward(
    const IOSTouchControlDefinition& control, const IOSTouchRect& frame,
    const IOSTouchInputCapture& capture, double current_time) {
  if (capture.began_time <= 0.0 ||
      (current_time - capture.began_time) > kMoveStickDoubleTapMaxSeconds) {
    return false;
  }
  const IOSTouchPoint unit =
      MoveStickUnitVectorForCapture(control, frame, capture);
  return unit.y <= -kMoveStickDoubleTapForwardThreshold &&
         std::abs(unit.x) <= kMoveStickDoubleTapLateralThreshold;
}

}  // namespace xe::hid::touch

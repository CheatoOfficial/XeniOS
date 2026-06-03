/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/ios/touch/touch_layout_library_ios.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <sstream>
#include <utility>

#include "xenia/config.h"
#include "xenia/ui/ios/touch/touch_overlay_style_ios.h"

namespace xe {
namespace ui {
namespace {

struct IOSTouchOfficialLayoutPreset {
  const char* local_id = nullptr;
  const char* display_name = nullptr;
  xe::hid::touch::IOSTouchLayoutModel (*factory)() = nullptr;
};

xe::hid::touch::IOSTouchRect MirrorTouchLayoutRectHorizontally(
    const xe::hid::touch::IOSTouchRect& rect) {
  return xe::hid::touch::IOSTouchRect{1.0f - rect.x - rect.width, rect.y,
                                      rect.width, rect.height};
}

xe::hid::touch::IOSTouchControlDefinition MakeOfficialTouchActionButton(
    const char* identifier, xe::hid::touch::IOSTouchAction action,
    const xe::hid::touch::IOSTouchRect& frame, uint8_t capture_priority,
    xe::hid::touch::IOSTouchTintStyle tint_style =
        xe::hid::touch::IOSTouchTintStyle::kAuto) {
  xe::hid::touch::IOSTouchControlDefinition control;
  control.identifier = identifier;
  control.type = xe::hid::touch::IOSTouchControlType::kActionButton;
  control.shape = xe::hid::touch::IOSTouchControlShape::kCircle;
  control.normalized_frame = frame;
  control.activation_radius = 0.5f;
  control.visual_opacity = 0.92f;
  control.capture_priority = capture_priority;
  control.tint_style = tint_style;
  xe::hid::touch::ConfigureIOSTouchControlAction(action, &control);
  return control;
}

xe::hid::touch::IOSTouchLayoutModel MakeOfficialIOSFPSStandardLayoutModel() {
  xe::hid::touch::IOSTouchLayoutModel layout =
      xe::hid::touch::CreateDefaultIOSFPSLayoutModel();
  layout.layout_id = kOfficialTouchLayoutLocalID;
  layout.display_name = "FPS Standard";
  layout.author = "XeniOS";
  layout.base_template = kOfficialTouchLayoutLocalID;
  return layout;
}

xe::hid::touch::IOSTouchLayoutModel MakeOfficialIOSFPSExpandedLayoutModel() {
  xe::hid::touch::IOSTouchLayoutModel layout =
      MakeOfficialIOSFPSStandardLayoutModel();
  layout.layout_id = kOfficialTouchLayoutExpandedLocalID;
  layout.display_name = "FPS Expanded";
  layout.base_template = kOfficialTouchLayoutExpandedLocalID;
  layout.controls.push_back(MakeOfficialTouchActionButton(
      "swap_button", xe::hid::touch::IOSTouchAction::kButtonY,
      xe::hid::touch::IOSTouchRect{0.84f, 0.36f, 0.11f, 0.13f}, 235,
      xe::hid::touch::IOSTouchTintStyle::kAmber));
  layout.controls.push_back(MakeOfficialTouchActionButton(
      "melee_button", xe::hid::touch::IOSTouchAction::kButtonB,
      xe::hid::touch::IOSTouchRect{0.88f, 0.66f, 0.11f, 0.13f}, 237,
      xe::hid::touch::IOSTouchTintStyle::kRose));
  layout.controls.push_back(MakeOfficialTouchActionButton(
      "left_bumper_button", xe::hid::touch::IOSTouchAction::kLeftBumper,
      xe::hid::touch::IOSTouchRect{0.58f, 0.49f, 0.11f, 0.13f}, 234,
      xe::hid::touch::IOSTouchTintStyle::kSky));
  layout.controls.push_back(MakeOfficialTouchActionButton(
      "right_bumper_button", xe::hid::touch::IOSTouchAction::kRightBumper,
      xe::hid::touch::IOSTouchRect{0.72f, 0.31f, 0.11f, 0.13f}, 233,
      xe::hid::touch::IOSTouchTintStyle::kSky));
  return layout;
}

xe::hid::touch::IOSTouchLayoutModel MakeOfficialIOSFPSMirroredLayoutModel() {
  xe::hid::touch::IOSTouchLayoutModel layout =
      MakeOfficialIOSFPSStandardLayoutModel();
  layout.layout_id = kOfficialTouchLayoutMirroredLocalID;
  layout.display_name = "FPS Mirrored";
  layout.base_template = kOfficialTouchLayoutMirroredLocalID;
  for (auto& control : layout.controls) {
    control.normalized_frame =
        MirrorTouchLayoutRectHorizontally(control.normalized_frame);
  }
  return layout;
}

const IOSTouchOfficialLayoutPreset kOfficialTouchLayoutPresets[] = {
    {kOfficialTouchLayoutLocalID, "FPS Standard",
     &MakeOfficialIOSFPSStandardLayoutModel},
    {kOfficialTouchLayoutExpandedLocalID, "FPS Expanded",
     &MakeOfficialIOSFPSExpandedLayoutModel},
    {kOfficialTouchLayoutMirroredLocalID, "FPS Mirrored",
     &MakeOfficialIOSFPSMirroredLayoutModel},
};

const IOSTouchOfficialLayoutPreset* FindOfficialTouchLayoutPreset(
    const std::string& local_id) {
  for (const auto& preset : kOfficialTouchLayoutPresets) {
    if (local_id == preset.local_id) {
      return &preset;
    }
  }
  return nullptr;
}

}  // namespace

bool IsOfficialTouchLayoutLocalID(const std::string& local_id) {
  return FindOfficialTouchLayoutPreset(local_id) != nullptr;
}

size_t OfficialTouchLayoutPresetSortOrder(const std::string& local_id) {
  for (size_t index = 0; index < std::size(kOfficialTouchLayoutPresets);
       ++index) {
    if (local_id == kOfficialTouchLayoutPresets[index].local_id) {
      return index;
    }
  }
  return std::size(kOfficialTouchLayoutPresets);
}

std::string NormalizeOfficialTouchLayoutBaseTemplate(
    std::string base_template) {
  if (FindOfficialTouchLayoutPreset(base_template)) {
    return base_template;
  }
  return kOfficialTouchLayoutLocalID;
}

std::string MakeTouchLayoutSlug(std::string value) {
  std::string slug;
  slug.reserve(value.size());
  bool last_was_separator = false;
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      slug.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      last_was_separator = false;
      continue;
    }
    if (!last_was_separator && !slug.empty()) {
      slug.push_back('_');
      last_was_separator = true;
    }
  }
  while (!slug.empty() && slug.back() == '_') {
    slug.pop_back();
  }
  return slug.empty() ? "touch_layout" : slug;
}

bool TryNormalizeConfiguredTouchLayoutLocalID(
    const std::string& configured_local_id,
    std::string* normalized_local_id_out) {
  if (!normalized_local_id_out || configured_local_id.empty()) {
    return false;
  }
  if (configured_local_id.find('/') != std::string::npos ||
      configured_local_id.find('\\') != std::string::npos ||
      configured_local_id.find("..") != std::string::npos) {
    return false;
  }
  std::string normalized_local_id = MakeTouchLayoutSlug(configured_local_id);
  if (normalized_local_id.empty() ||
      normalized_local_id != configured_local_id) {
    return false;
  }
  *normalized_local_id_out = std::move(normalized_local_id);
  return true;
}

std::string TouchLayoutBaseTemplateForTable(const toml::table& table) {
  if (auto base_template = table["base_template"].value<std::string>()) {
    return NormalizeOfficialTouchLayoutBaseTemplate(*base_template);
  }
  if (auto layout_id = table["layout_id"].value<std::string>()) {
    return NormalizeOfficialTouchLayoutBaseTemplate(*layout_id);
  }
  return kOfficialTouchLayoutLocalID;
}

xe::hid::touch::IOSTouchLayoutModel MakeTouchLayoutSeedModelForTable(
    const toml::table& table) {
  return MakeOfficialIOSTouchLayoutModelForLocalID(
      TouchLayoutBaseTemplateForTable(table));
}

xe::hid::touch::IOSTouchLayoutModel MakeOfficialIOSTouchLayoutModelForLocalID(
    const std::string& local_id) {
  if (const auto* preset = FindOfficialTouchLayoutPreset(local_id)) {
    return preset->factory();
  }
  return MakeOfficialIOSFPSStandardLayoutModel();
}

xe::hid::touch::IOSTouchLayoutModel MakeOfficialIOSTouchLayoutModel() {
  return MakeOfficialIOSTouchLayoutModelForLocalID(
      kOfficialTouchLayoutLocalID);
}

UIImage* RenderTouchLayoutThumbnail(
    const xe::hid::touch::IOSTouchLayoutModel& layout, CGSize size) {
  if (size.width <= 0.0 || size.height <= 0.0) {
    return nil;
  }
  UIGraphicsImageRendererFormat* format =
      [UIGraphicsImageRendererFormat preferredFormat];
  format.opaque = NO;
  format.scale = 0.0;
  UIGraphicsImageRenderer* renderer =
      [[[UIGraphicsImageRenderer alloc] initWithSize:size format:format]
          autorelease];
  return [renderer imageWithActions:^(UIGraphicsImageRendererContext* ctx) {
    CGContextRef cg = ctx.CGContext;
    CGContextSetFillColorWithColor(
        cg, [UIColor colorWithWhite:0.10 alpha:0.95].CGColor);
    CGContextFillRect(cg, CGRectMake(0, 0, size.width, size.height));

    for (const auto& control : layout.controls) {
      const CGRect frame = CGRectMake(
          control.normalized_frame.x * size.width,
          control.normalized_frame.y * size.height,
          MAX(control.normalized_frame.width * size.width, 2.0),
          MAX(control.normalized_frame.height * size.height, 2.0));
      UIColor* tint =
          XeniaTouchOverlayAccentColor(control.tint_style, control.type);
      CGContextSetFillColorWithColor(
          cg, [tint colorWithAlphaComponent:0.55].CGColor);
      CGContextSetStrokeColorWithColor(cg, tint.CGColor);
      CGContextSetLineWidth(cg, 0.5);
      const CGFloat corner =
          control.shape == xe::hid::touch::IOSTouchControlShape::kCircle
              ? MIN(frame.size.width, frame.size.height) * 0.5
              : MIN(MIN(frame.size.width, frame.size.height) * 0.30, 4.0);
      UIBezierPath* path =
          [UIBezierPath bezierPathWithRoundedRect:frame cornerRadius:corner];
      [path fill];
      [path stroke];
    }
  }];
}

std::string ReadTitleTouchLayoutAssignment(uint32_t title_id) {
  if (!title_id) {
    return std::string();
  }
  toml::table config = config::LoadGameConfig(title_id);
  const toml::table* assignment =
      config[kTouchLayoutAssignmentSection].as_table();
  if (!assignment) {
    return std::string();
  }
  auto local_layout_id = (*assignment)["local_layout_id"].value<std::string>();
  if (!local_layout_id) {
    return std::string();
  }
  std::string normalized;
  if (!TryNormalizeConfiguredTouchLayoutLocalID(*local_layout_id,
                                                &normalized)) {
    return std::string();
  }
  return normalized;
}

bool TouchLayoutContentMatches(
    const xe::hid::touch::IOSTouchLayoutModel& a,
    const xe::hid::touch::IOSTouchLayoutModel& b) {
  std::ostringstream stream_a;
  std::ostringstream stream_b;
  stream_a << xe::hid::touch::EncodeIOSTouchLayoutModel(a);
  stream_b << xe::hid::touch::EncodeIOSTouchLayoutModel(b);
  return stream_a.str() == stream_b.str();
}

}  // namespace ui
}  // namespace xe

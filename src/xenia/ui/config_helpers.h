/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_CONFIG_HELPERS_H_
#define XENIA_UI_CONFIG_HELPERS_H_

#include <map>
#include <string>
#include <vector>

#include "xenia/base/platform.h"

namespace xe {
namespace ui {

// Known enum-like cvars with their valid options
inline const std::map<std::string, std::vector<std::string>>&
GetKnownEnumOptions() {
  static const std::map<std::string, std::vector<std::string>> options = {
#if XE_PLATFORM_WIN32
      {"gpu", {"any", "d3d12", "vulkan", "null"}},
      {"apu", {"any", "nop", "sdl", "xaudio2"}},
      {"hid", {"any", "nop", "sdl", "winkey", "xinput"}},
#elif XE_PLATFORM_LINUX
      {"gpu", {"any", "vulkan", "null"}},
      {"apu", {"any", "alsa", "nop", "sdl"}},
      {"hid", {"any", "nop", "sdl"}},
#else
      {"gpu", {"any", "vulkan", "null"}},
      {"apu", {"any", "nop", "sdl"}},
      {"hid", {"any", "nop", "sdl"}},
#endif
      {"d3d12_readback_resolve",
       {"kCopy", "kComputeLuminance", "kComputeRGBA16"}},
      {"render_target_path_d3d12", {"any", "rtv", "rov"}},
      {"render_target_path_vulkan", {"any", "fbo", "fsi"}},
      {"postprocess_antialiasing", {"off", "fxaa", "fxaa_extreme"}},
      {"postprocess_scaling_and_sharpening",
       {"bilinear", "cas", "fsr", "nearest"}},
      {"spirv_version_override", {"auto", "1.0", "1.3", "1.4", "1.5", "1.6"}},
      {"xma_decoder", {"old", "new", "fake"}},
      {"user_language",
       {"English", "Japanese", "German", "French", "Spanish", "Italian",
        "Korean", "TChinese", "Portuguese", "SChinese", "Polish", "Russian"}},
      {"user_country",
       {"UAE",
        "Albania",
        "Armenia",
        "Argentina",
        "Austria",
        "Australia",
        "Azerbaijan",
        "Belgium",
        "Bulgaria",
        "Bahrain",
        "Brunei",
        "Bolivia",
        "Brazil",
        "Belarus",
        "Belize",
        "Canada",
        "Switzerland",
        "Chile",
        "China",
        "Colombia",
        "Costa Rica",
        "Czech Republic",
        "Germany",
        "Denmark",
        "Dominican Republic",
        "Algeria",
        "Ecuador",
        "Estonia",
        "Egypt",
        "Spain",
        "Finland",
        "Faroe Islands",
        "France",
        "Great Britain",
        "Georgia",
        "Greece",
        "Guatemala",
        "Hong Kong",
        "Honduras",
        "Croatia",
        "Hungary",
        "Indonesia",
        "Ireland",
        "Israel",
        "India",
        "Iraq",
        "Iran",
        "Iceland",
        "Italy",
        "Jamaica",
        "Jordan",
        "Japan",
        "Kenya",
        "Kyrgyzstan",
        "Korea",
        "Kuwait",
        "Kazakhstan",
        "Lebanon",
        "Liechtenstein",
        "Lithuania",
        "Luxembourg",
        "Latvia",
        "Libya",
        "Morocco",
        "Monaco",
        "Macedonia",
        "Mongolia",
        "Macau",
        "Maldives",
        "Mexico",
        "Malaysia",
        "Nicaragua",
        "Netherlands",
        "Norway",
        "New Zealand",
        "Oman",
        "Panama",
        "Peru",
        "Philippines",
        "Pakistan",
        "Poland",
        "Puerto Rico",
        "Portugal",
        "Paraguay",
        "Qatar",
        "Romania",
        "Russia",
        "Saudi Arabia",
        "Sweden",
        "Singapore",
        "Slovenia",
        "Slovakia",
        "El Salvador",
        "Syria",
        "Thailand",
        "Tunisia",
        "Turkey",
        "Trinidad",
        "Taiwan",
        "Ukraine",
        "United States",
        "Uruguay",
        "Uzbekistan",
        "Venezuela",
        "Vietnam",
        "Yemen",
        "South Africa",
        "Zimbabwe"}},
  };
  return options;
}

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_CONFIG_HELPERS_H_

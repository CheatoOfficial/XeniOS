/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_GAME_COMPAT_DB_H_
#define XENIA_APP_GAME_COMPAT_DB_H_

#include <cstdint>

namespace xe {
namespace app {

// Order encodes "better compat" — higher value sorts above.
enum class CompatState : uint8_t {
  kUnknown = 0,
  kUnplayable,
  kLoads,
  kGameplay,
  kPlayable,
};

CompatState GetCompatState(uint32_t title_id);

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_GAME_COMPAT_DB_H_

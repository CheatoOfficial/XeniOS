/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_
#define XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_

#include "xenia/base/vec128.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class StackLayout {
 public:
  /**
   * ARM64 Thunk Stack Layout (HostToGuest)
   * NOTE: stack must always be 16-byte aligned.
   *
   * Thunk stack:
   *      Non-Volatile         Volatile
   *  +------------------+------------------+
   *  | arg temp, 4 * 8  | arg temp, 4 * 8  | sp + 0x000
   *  |                  |                  |
   *  |                  |                  |
   *  +------------------+------------------+
   *  | rbx              | (unused)         | sp + 0x018
   *  +------------------+------------------+
   *  | rbp              | X1               | sp + 0x020
   *  +------------------+------------------+
   *  | rcx (Win32)      | X2               | sp + 0x028
   *  +------------------+------------------+
   *  | rsi (Win32)      | X3               | sp + 0x030
   *  +------------------+------------------+
   *  | rdi (Win32)      | X4               | sp + 0x038
   *  +------------------+------------------+
   *  | r12              | X5               | sp + 0x040
   *  +------------------+------------------+
   *  | r13              | X6               | sp + 0x048
   *  +------------------+------------------+
   *  | r14              | X7               | sp + 0x050
   *  +------------------+------------------+
   *  | r15              | X8               | sp + 0x058
   *  +------------------+------------------+
   *  | xmm6 (Win32)     | X9               | sp + 0x060
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm7 (Win32)     | X10              | sp + 0x070
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm8 (Win32)     | X11              | sp + 0x080
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm9 (Win32)     | X12              | sp + 0x090
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm10 (Win32)    | X13              | sp + 0x0A0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm11 (Win32)    | X14              | sp + 0x0B0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm12 (Win32)    | X15              | sp + 0x0C0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm13 (Win32)    | X16              | sp + 0x0D0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm14 (Win32)    | X17              | sp + 0x0E0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm15 (Win32)    | X18              | sp + 0x0F0
   *  |                  |                  |
   *  +------------------+------------------+
   */
  XEPACKEDSTRUCT(Thunk, {
    uint64_t arg_temp[4];
    uint64_t r[18];
    vec128_t xmm[31];
  });
  static_assert(sizeof(Thunk) % 16 == 0,
                "sizeof(Thunk) must be a multiple of 16!");
  static const size_t THUNK_STACK_SIZE = sizeof(Thunk);

  /**
   * ARM64 Guest Stack Layout
   *  +------------------+
   *  | scratch, 48b     | sp + 0x000  (3 x Q for VMX FP scratch)
   *  | guest ret addr   | sp + 0x030  (guest PPC return address)
   *  | call ret addr    | sp + 0x038  (next call's guest PPC return addr)
   *  | host ret addr    | sp + 0x040  (host x30/LR, for ret instruction)
   *  | guest saved r1   | sp + 0x048  (guest r1 at function entry, for
   *  |                  |              longjmp detection)
   *  |  ... locals ...  |
   *  +------------------+
   *
   * Minimum size: 80 bytes (aligned to 16).
   *
   * Convention: at guest function entry, x0 holds the guest PPC return
   * address. The prolog stores it to GUEST_RET_ADDR and saves x30 (host
   * LR) to HOST_RET_ADDR.
   */
  static constexpr size_t GUEST_STACK_SIZE = 80;  // 16-byte aligned
  static constexpr size_t GUEST_SCRATCH = 0;      // 48 bytes (3 x Q)
  static constexpr size_t GUEST_RET_ADDR = 48;
  static constexpr size_t GUEST_CALL_RET_ADDR = 56;
  static constexpr size_t HOST_RET_ADDR = 64;
  // Stackpoint depth after PushStackpoint in prolog, for longjmp detection.
  static constexpr size_t GUEST_SAVED_STACKPOINT_DEPTH = 72;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_

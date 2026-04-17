/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_
#define XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_

#include <atomic>
#include <memory>
#include <mutex>

#include "xenia/cpu/backend/code_cache_base.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64CodeCache : public CodeCacheBase<A64CodeCache> {
 public:
  ~A64CodeCache() override = default;

  static std::unique_ptr<A64CodeCache> Create();

  virtual bool Initialize();

  void* LookupUnwindInfo(uint64_t host_pc) override { return nullptr; }

  // Override execute_base_address to return the actual allocated address
  // (which may differ from kGeneratedCodeExecuteBase on platforms where
  // the fixed mapping fails and we fall back to an OS-chosen address).
  uintptr_t execute_base_address() const override;

  // CRTP hooks for CodeCacheBase.
  void FillCode(void* write_address, size_t size);
  void FlushCodeRange(void* address, size_t size);

  // CRTP hook: write indirection entry using rel32 + tagged encoding.
  void UpdateIndirection(uint32_t guest_address, void* code_execute_address);

  // Hide base AddIndirection/CommitExecutableRange with relocatable versions.
  void AddIndirection(uint32_t guest_address, uint32_t host_address);
  void AddIndirection64(uint32_t guest_address, uint64_t host_address);
  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high);

  // Set indirection default using 64-bit encoding (for resolve thunk).
  void set_indirection_default_64(uint64_t default_value);

  // --- Relocatable indirection support ---

  // Tag bit set in indirection entries that index into the external table
  // instead of storing a rel32 code-cache offset.
  static constexpr uint32_t kIndirectionExternalTag = 0x80000000u;
  static constexpr uint32_t kIndirectionExternalIndexMask = 0x7FFFFFFFu;
  static constexpr uint32_t kIndirectionExternalCapacity = 0x00010000u;

  // Encode a host address as a 32-bit indirection entry:
  //  - rel32 offset from code cache base (bit 31 clear) if within code cache
  //  - tagged index into external table  (bit 31 set)   otherwise
  uint32_t EncodeIndirectionTarget(uint64_t host_address);

  // Accessors for the emitter to bake as 64-bit immediates.
  uintptr_t indirection_table_base_bias() const {
    return indirection_table_base_bias_;
  }
  uintptr_t external_indirection_table_base_address() const {
    return reinterpret_cast<uintptr_t>(external_indirection_targets_.get());
  }
  uintptr_t indirection_table_base_address() const {
    return indirection_table_actual_base_;
  }

  // Virtual for platform-specific overrides (_win.cc / _posix.cc).
  virtual UnwindReservation RequestUnwindReservation(uint8_t* entry_address) {
    return UnwindReservation();
  }
  virtual void PlaceCode(uint32_t guest_address, void* machine_code,
                         const EmitFunctionInfo& func_info,
                         void* code_execute_address,
                         UnwindReservation unwind_reservation) {}

 protected:
  A64CodeCache() = default;

  // Actual allocated base of the indirection table (may differ from
  // kIndirectionTableBase when the fixed mapping is unavailable).
  uintptr_t indirection_table_actual_base_ = 0;

  // indirection_table_actual_base_ - kIndirectionTableBase.
  // The emitter adds this to a guest address to compute the slot pointer.
  uintptr_t indirection_table_base_bias_ = 0;

  // Side table holding full 64-bit host addresses for targets outside the
  // contiguous code cache (resolve thunks, trampolines, etc.).
  std::unique_ptr<uint64_t[]> external_indirection_targets_;
  std::atomic<uint32_t> external_indirection_target_count_{0};
  std::mutex external_indirection_mutex_;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_

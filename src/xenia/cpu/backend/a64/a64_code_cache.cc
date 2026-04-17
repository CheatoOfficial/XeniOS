/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_code_cache.h"

#include <cstring>
#include <mutex>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform.h"
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

bool A64CodeCache::Initialize() {
  // --- Indirection table: allocate at an OS-chosen address ---
  // We no longer require the table to live at host VA 0x80000000.
  // The emitter compensates via indirection_table_base_bias_.
  indirection_table_base_ = reinterpret_cast<uint8_t*>(xe::memory::AllocFixed(
      nullptr, kIndirectionTableSize, xe::memory::AllocationType::kReserve,
      xe::memory::PageAccess::kReadWrite));
  if (!indirection_table_base_) {
    XELOGE("Unable to reserve indirection table at any address (size=0x{:X})",
           static_cast<uint64_t>(kIndirectionTableSize));
    return false;
  }
  indirection_table_actual_base_ =
      reinterpret_cast<uintptr_t>(indirection_table_base_);
  indirection_table_base_bias_ = indirection_table_actual_base_ -
                                 static_cast<uintptr_t>(kIndirectionTableBase);
  XELOGI(
      "A64 indirection table: guest_base=0x{:08X} table_base=0x{:016X} "
      "bias=0x{:016X}",
      static_cast<uint32_t>(kIndirectionTableBase),
      static_cast<uint64_t>(indirection_table_actual_base_),
      static_cast<uint64_t>(indirection_table_base_bias_));

  // --- External indirection targets (for out-of-cache addresses) ---
  external_indirection_targets_ =
      std::make_unique<uint64_t[]>(kIndirectionExternalCapacity);
  if (!external_indirection_targets_) {
    XELOGE("Unable to allocate external indirection table (entries={})",
           static_cast<uint32_t>(kIndirectionExternalCapacity));
    return false;
  }
  external_indirection_target_count_.store(0, std::memory_order_relaxed);

  // --- File-backed mapping for the generated code region ---
  file_name_ = fmt::format("xenia_code_cache_{}", Clock::QueryHostTickCount());
  mapping_ = xe::memory::CreateFileMappingHandle(
      file_name_, kGeneratedCodeSize, xe::memory::PageAccess::kExecuteReadWrite,
      false);
  if (mapping_ == xe::memory::kFileMappingHandleInvalid) {
    XELOGE("Unable to create code cache mmap");
    return false;
  }

  // --- Map the generated code region ---
  // Try the preferred fixed address first; fall back to OS-chosen on failure.
  if (xe::memory::IsWritableExecutableMemoryPreferred()) {
    generated_code_execute_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
            mapping_, reinterpret_cast<void*>(kGeneratedCodeExecuteBase),
            kGeneratedCodeSize, xe::memory::PageAccess::kExecuteReadWrite, 0));
    if (!generated_code_execute_base_) {
      XELOGW(
          "Fixed address mapping for generated code failed at 0x{:X}, "
          "trying OS-chosen address",
          static_cast<uint64_t>(kGeneratedCodeExecuteBase));
      generated_code_execute_base_ =
          reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
              mapping_, nullptr, kGeneratedCodeSize,
              xe::memory::PageAccess::kExecuteReadWrite, 0));
    }
    generated_code_write_base_ = generated_code_execute_base_;
    if (!generated_code_execute_base_) {
      XELOGE("Unable to allocate code cache generated code storage");
      return false;
    }
  } else {
    // W^X split: separate execute and write views.
    generated_code_execute_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
            mapping_, reinterpret_cast<void*>(kGeneratedCodeExecuteBase),
            kGeneratedCodeSize, xe::memory::PageAccess::kExecuteReadOnly, 0));
    if (!generated_code_execute_base_) {
      XELOGW(
          "Fixed address mapping for execute view failed at 0x{:X}, "
          "trying OS-chosen address",
          static_cast<uint64_t>(kGeneratedCodeExecuteBase));
      generated_code_execute_base_ = reinterpret_cast<uint8_t*>(
          xe::memory::MapFileView(mapping_, nullptr, kGeneratedCodeSize,
                                  xe::memory::PageAccess::kExecuteReadOnly, 0));
    }
    generated_code_write_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
            mapping_, reinterpret_cast<void*>(kGeneratedCodeWriteBase),
            kGeneratedCodeSize, xe::memory::PageAccess::kReadWrite, 0));
    if (!generated_code_write_base_) {
      XELOGW(
          "Fixed address mapping for write view failed, "
          "trying OS-chosen address");
      generated_code_write_base_ = reinterpret_cast<uint8_t*>(
          xe::memory::MapFileView(mapping_, nullptr, kGeneratedCodeSize,
                                  xe::memory::PageAccess::kReadWrite, 0));
    }
    if (!generated_code_execute_base_ || !generated_code_write_base_) {
      XELOGE("Unable to allocate code cache generated code storage");
      return false;
    }
  }

  XELOGI("A64 code cache: execute_base=0x{:016X} write_base=0x{:016X}",
         reinterpret_cast<uint64_t>(generated_code_execute_base_),
         reinterpret_cast<uint64_t>(generated_code_write_base_));

  generated_code_map_.reserve(kMaximumFunctionCount);
  return true;
}

uintptr_t A64CodeCache::execute_base_address() const {
  return generated_code_execute_base_
             ? reinterpret_cast<uintptr_t>(generated_code_execute_base_)
             : kGeneratedCodeExecuteBase;
}

uint32_t A64CodeCache::EncodeIndirectionTarget(uint64_t host_address) {
  const uintptr_t code_base = execute_base_address();
  const uintptr_t code_end = code_base + kGeneratedCodeSize;
  if (host_address >= code_base && host_address < code_end) {
    // Fast path: rel32 offset from code cache base (bit 31 always clear
    // because kGeneratedCodeSize = 0x0FFFFFFF < 0x80000000).
    return static_cast<uint32_t>(host_address - code_base);
  }

  // Slow path: allocate an external table entry (or reuse existing).
  std::lock_guard<std::mutex> lock(external_indirection_mutex_);
  const uint32_t current_count =
      external_indirection_target_count_.load(std::memory_order_relaxed);

  // Deduplicate — table is small (dozens of entries), linear scan is fine.
  for (uint32_t i = 0; i < current_count; i++) {
    if (external_indirection_targets_[i] == host_address) {
      return kIndirectionExternalTag | i;
    }
  }

  if (current_count >= kIndirectionExternalCapacity) {
    XELOGE(
        "A64 indirection external table overflow (count={} capacity={}); "
        "falling back to default target",
        current_count, static_cast<uint32_t>(kIndirectionExternalCapacity));
    return indirection_default_value_;
  }

  external_indirection_targets_[current_count] = host_address;
  external_indirection_target_count_.store(current_count + 1,
                                           std::memory_order_release);
  return kIndirectionExternalTag | current_count;
}

void A64CodeCache::set_indirection_default_64(uint64_t default_value) {
  indirection_default_value_ = EncodeIndirectionTarget(default_value);
}

void A64CodeCache::UpdateIndirection(uint32_t guest_address,
                                     void* code_execute_address) {
  if (guest_address < kIndirectionTableBase) {
    return;
  }
  const uint64_t guest_delta = guest_address - kIndirectionTableBase;
  const uint64_t slot_offset = (guest_delta / 4) * 4;
  if (slot_offset + 4 > kIndirectionTableSize) {
    return;
  }
  uint32_t* slot =
      reinterpret_cast<uint32_t*>(indirection_table_base_ + slot_offset);
  *slot =
      EncodeIndirectionTarget(reinterpret_cast<uint64_t>(code_execute_address));
}

void A64CodeCache::AddIndirection(uint32_t guest_address,
                                  uint32_t host_address) {
  AddIndirection64(guest_address, static_cast<uint64_t>(host_address));
}

void A64CodeCache::AddIndirection64(uint32_t guest_address,
                                    uint64_t host_address) {
  if (!indirection_table_base_) {
    return;
  }
  if (guest_address < kIndirectionTableBase) {
    return;
  }
  const uint64_t guest_delta = guest_address - kIndirectionTableBase;
  const uint64_t slot_offset = (guest_delta / 4) * 4;
  if (slot_offset + 4 > kIndirectionTableSize) {
    return;
  }
  uint32_t* slot =
      reinterpret_cast<uint32_t*>(indirection_table_base_ + slot_offset);
  *slot = EncodeIndirectionTarget(host_address);
}

void A64CodeCache::CommitExecutableRange(uint32_t guest_low,
                                         uint32_t guest_high) {
  if (!indirection_table_base_) {
    return;
  }
  if (guest_low < kIndirectionTableBase) {
    return;
  }

  const size_t start_offset =
      static_cast<size_t>(guest_low - kIndirectionTableBase);
  const size_t size = static_cast<size_t>(guest_high - guest_low);

  if (start_offset + size > kIndirectionTableSize) {
    XELOGE("CommitExecutableRange: range [0x{:08X}, 0x{:08X}) exceeds table",
           guest_low, guest_high);
    return;
  }

  xe::memory::AllocFixed(indirection_table_base_ + start_offset, size,
                         xe::memory::AllocationType::kCommit,
                         xe::memory::PageAccess::kReadWrite);

  uint32_t* p =
      reinterpret_cast<uint32_t*>(indirection_table_base_ + start_offset);
  const size_t entry_count = size / 4;
  for (size_t i = 0; i < entry_count; i++) {
    p[i] = indirection_default_value_;
  }
}

void A64CodeCache::FillCode(void* write_address, size_t size) {
  // Fill with BRK #0 (0xD4200000), 4-byte aligned.
  constexpr uint32_t kBrk0 = 0xD4200000;
  auto* p = reinterpret_cast<uint32_t*>(write_address);
  auto* end =
      reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(write_address) + size);
  for (; p < end; ++p) {
    *p = kBrk0;
  }
}

void A64CodeCache::FlushCodeRange(void* address, size_t size) {
#if XE_PLATFORM_WIN32
  FlushInstructionCache(GetCurrentProcess(), address, size);
#else
  __builtin___clear_cache(
      reinterpret_cast<char*>(address),
      reinterpret_cast<char*>(static_cast<uint8_t*>(address) + size));
#endif
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

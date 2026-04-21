/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_emitter.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>

#include <cctype>
#include <climits>
#include <cstring>

#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/a64/a64_seq_util.h"
#include "xenia/cpu/backend/a64/a64_sequences.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/symbol.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/cpu/xex_module.h"

DECLARE_int64(a64_max_stackpoints);
DECLARE_bool(a64_enable_host_guest_stack_synchronization);

DEFINE_bool(debugprint_trap_log, false,
            "Log debugprint traps to the active debugger", "CPU");
DEFINE_bool(ignore_undefined_externs, true,
            "Don't exit when an undefined extern is called.", "CPU");
DEFINE_bool(log_undefined_extern_args, false,
            "Log PPC args for undefined externs (once per function).", "CPU");
DEFINE_bool(emit_source_annotations, false,
            "Add extra movs and nops to make disassembly easier to read.",
            "CPU");
DEFINE_bool(a64_resolve_function_log, false,
            "Log A64 ResolveFunction failures with module ranges.", "CPU");
DEFINE_int32(a64_resolve_function_log_limit, 8,
             "Maximum ResolveFunction failure logs.", "CPU");
DEFINE_bool(a64_stack_sync_log, false, "Log A64 stack sync events.", "CPU");
DEFINE_int32(a64_stack_sync_log_limit, 16, "Maximum A64 stack sync logs.",
             "CPU");
DEFINE_bool(a64_stack_sync_check, false,
            "Emit runtime checks for guest/host stack mismatches after calls.",
            "CPU");
DEFINE_bool(a64_fail_fast_on_trap, true,
            "Exit immediately on forced A64 traps to avoid infinite log spam.",
            "a64");

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace Xbyak_aarch64;

// Defined in a64_backend.cc.
extern uint64_t ResolveFunction(void* raw_context, uint64_t target_address);

static uint64_t UndefinedCallExtern(void* raw_context, uint64_t function_ptr) {
  auto function = reinterpret_cast<Function*>(function_ptr);
  XELOGE("undefined extern call to {:08X} {}", function->address(),
         function->name());
  return 0;
}

static constexpr size_t kMaxCodeSize = 1_MiB;

// Register maps:
// GPR allocatable registers: x22, x23, x24, x25, x26, x27, x28
// (x19=backend context, x20=context, x21=membase are reserved)
const uint32_t A64Emitter::gpr_reg_map_[GPR_COUNT] = {
    22, 23, 24, 25, 26, 27, 28,
};

// VEC allocatable registers: v4-v15, v16-v31
// (v0-v3 are scratch)
const uint32_t A64Emitter::vec_reg_map_[VEC_COUNT] = {
    4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
};

A64Emitter::A64Emitter(A64Backend* backend, XbyakA64Allocator* allocator)
    : CodeGenerator(kMaxCodeSize, Xbyak_aarch64::DontSetProtectRWE, allocator),
      processor_(backend->processor()),
      backend_(backend),
      code_cache_(backend->code_cache()),
      allocator_(allocator),
      feature_flags_(arm64::GetFeatureFlags()) {}

A64Emitter::~A64Emitter() = default;

void A64Emitter::LoadConstantV(const Xbyak_aarch64::QReg& reg, float value) {
  union {
    float f;
    uint32_t u;
  } c;
  c.f = value;
  mov(w17, static_cast<uint64_t>(c.u));
  fmov(Xbyak_aarch64::SReg(reg.getIdx()), w17);
}

void A64Emitter::LoadConstantV(const Xbyak_aarch64::QReg& reg, double value) {
  union {
    double d;
    uint64_t u;
  } c;
  c.d = value;
  mov(x17, c.u);
  fmov(Xbyak_aarch64::DReg(reg.getIdx()), x17);
}

void A64Emitter::LoadConstantV(const Xbyak_aarch64::QReg& reg,
                               const vec128_t& value, int gpr_scratch_idx) {
  LoadV128Const(*this, reg.getIdx(), value, gpr_scratch_idx);
}

void A64Emitter::LoadConstantV(const Xbyak_aarch64::VReg& reg,
                               const vec128_t& value, int gpr_scratch_idx) {
  LoadV128Const(*this, reg.getIdx(), value, gpr_scratch_idx);
}

bool A64Emitter::Emit(GuestFunction* function, hir::HIRBuilder* builder,
                      uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
                      void** out_code_address, size_t* out_code_size,
                      std::vector<SourceMapEntry>* out_source_map) {
  SCOPE_profile_cpu_f("cpu");

  guest_module_ = dynamic_cast<XexModule*>(function->module());

  // Reset.
  debug_info_ = debug_info;
  debug_info_flags_ = debug_info_flags;
  trace_data_ = &function->trace_data();

  current_guest_function_ = function->address();

  // Reset state.
  stack_size_ = StackLayout::GUEST_STACK_SIZE;
  source_map_arena_.Reset();
  tail_code_.clear();
  fpcr_mode_ = FPCRMode::Unknown;

  // Try to emit.
  EmitFunctionInfo func_info = {};
  if (!Emit(builder, func_info)) {
    return false;
  }

  // Emplace the code into the code cache.
  *out_code_address = Emplace(func_info, function);
  *out_code_size = func_info.code_size.total;

  // Copy source map.
  source_map_arena_.CloneContents(out_source_map);

  return *out_code_address != nullptr;
}

void* A64Emitter::Emplace(const EmitFunctionInfo& func_info,
                          GuestFunction* function) {
  // Copy the current oaknut instruction-buffer into the code-cache
  void* new_execute_address;
  void* new_write_address;

  assert_true(func_info.code_size.total == static_cast<size_t>(offset()));

  if (function) {
    code_cache_->PlaceGuestCode(function->address(), assembly_buffer.data(),
                                func_info, function, new_execute_address,
                                new_write_address);
  } else {
    code_cache_->PlaceHostCode(0, assembly_buffer.data(), func_info,
                               new_execute_address, new_write_address);
  }

  // Reset the oaknut instruction-buffer
  assembly_buffer.clear();
  label_lookup_.clear();

  return new_execute_address;
}

void A64Emitter::EmitBtiJc() {
  // Accept both indirect calls (BLR) and jumps (BR) at JIT entry points.
  // 0xD50324DF encodes "bti jc".
  dw(0xD50324DF);
}

bool A64Emitter::Emit(HIRBuilder* builder, EmitFunctionInfo& func_info) {
  oaknut::Label epilog_label;
  epilog_label_ = &epilog_label;

  // Calculate stack size. We need to align things to their natural sizes.
  // This could be much better (sort by type/etc).
  auto locals = builder->locals();
  size_t stack_offset = StackLayout::GUEST_STACK_SIZE;
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    auto slot = *it;
    size_t type_size = hir::GetTypeSize(slot->type);
    // Align to natural size (at least 4 bytes for ARM64 alignment).
    size_t align_size = xe::round_up(type_size, static_cast<size_t>(4));
    stack_offset = xe::align(stack_offset, align_size);
    slot->set_constant(static_cast<uint32_t>(stack_offset));
    stack_offset += type_size;
  }
  // Align total stack offset to 16 bytes (ARM64 ABI requirement).
  stack_offset -= StackLayout::GUEST_STACK_SIZE;
  stack_offset = xe::align(stack_offset, static_cast<size_t>(16));

  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  // ARM64 ABI: SP must always be 16-byte aligned.
  assert_true(stack_size % 16 == 0);
  func_info.stack_size = stack_size;
  func_info.lr_save_offset = StackLayout::HOST_RET_ADDR;
  stack_size_ = stack_size;

  struct {
    size_t prolog;
    size_t body;
    size_t epilog;
    size_t tail;
    size_t prolog_stack_alloc;
  } code_offsets = {};

  // ========================================================================
  // PROLOG
  // ========================================================================
  code_offsets.prolog = getSize();

  // sub sp, sp, #stack_size
  if (stack_size <= 4095) {
    sub(sp, sp, static_cast<uint32_t>(stack_size));
  } else {
    mov(x17, static_cast<uint64_t>(stack_size));
    sub(sp, sp, x17, UXTX);
  }
  code_offsets.prolog_stack_alloc = getSize();

  // Store host return address (x30/LR) so the epilog can restore it.
  str(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
  // Store guest PPC return address (passed in x0 by convention).
  str(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
  // Store zero for call return address (we haven't made a call yet).
  str(xzr, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));

  STP(X29, X30, SP, PRE_INDEXED, -16);
  MOV(X29, SP);

  PushStackpoint();

  AdjustStackPointer(*this, stack_size, false);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  STR(GetContextReg(), SP, StackLayout::GUEST_CTX_HOME);
  STR(X0, SP, StackLayout::GUEST_RET_ADDR);
  STR(XZR, SP, StackLayout::GUEST_CALL_RET_ADDR);

  // Safe now to do some tracing.
  if (debug_info_flags_ & DebugInfoFlags::kDebugInfoTraceFunctions) {
    // We require 32-bit addresses.
    assert_true(uint64_t(trace_data_->header()) < UINT_MAX);
    auto trace_header = trace_data_->header();

    // Call count.
    MOV(W0, 1);
    MOV(X5, reinterpret_cast<uintptr_t>(
                low_address(&trace_header->function_call_count)));
    LDADDAL(X0, X0, X5);

    // Get call history slot.
    static_assert(FunctionTraceData::kFunctionCallerHistoryCount == 4,
                  "bitmask depends on count");
    LDR(X0, X5);
    AND(W0, W0, 0b00000011);

    // Record call history value into slot (guest addr in W1).
    MOV(X5, reinterpret_cast<uintptr_t>(
                low_address(&trace_header->function_caller_history)));
    STR(W1, X5, X0, oaknut::IndexExt::LSL, 2);

    // Calling thread. Load X0 with thread ID.
    EmitGetCurrentThreadId();
    MOV(W5, 1);
    LSL(W0, W5, W0);

    MOV(X5, reinterpret_cast<uintptr_t>(
                low_address(&trace_header->function_thread_use)));
    LDSET(W0, WZR, X5);
  }

  // ========================================================================
  // BODY
  // ========================================================================
  code_offsets.body = getSize();

  // Allocate the epilog label (owned by label_cache_ for cleanup).
  auto epilog_label_ptr = new Label();
  label_cache_.push_back(epilog_label_ptr);
  epilog_label_ = epilog_label_ptr;

  // Walk HIR blocks and emit ARM64 instructions.
  auto block = builder->first_block();
  synchronize_stack_on_next_instruction_ = false;
  while (block) {
    // Reset FPCR tracking on each block entry (we don't know which
    // predecessor ran, so mode is unknown).
    ForgetFpcrMode();

    // Bind all labels targeting this block.
    auto label = block->label_head;
    while (label) {
      L(GetLabel(label->id));
      label = label->next;
    }

    // Process each instruction in the block.
    const hir::Instr* instr = block->instr_head;
    while (instr) {
      // After a guest call, check for longjmp on the next real instruction.
      // Skip SOURCE_OFFSET because the return address from the call would
      // point past the check, so it would never execute.
      if (synchronize_stack_on_next_instruction_) {
        if (instr->GetOpcodeNum() != hir::OPCODE_SOURCE_OFFSET) {
          synchronize_stack_on_next_instruction_ = false;
          EnsureSynchronizedGuestAndHostStack();
        }
      }
      const hir::Instr* new_tail = instr;
      if (!SelectSequence(this, instr, &new_tail)) {
        // No sequence matched — this is expected in Phase 1 before
        // sequences are implemented.
        XELOGE("A64: Unable to process HIR opcode {}",
               hir::GetOpcodeName(instr->GetOpcodeInfo()));
        return false;
      }
      instr = new_tail;
    }

    block = block->next;
  }

  // ========================================================================
  // EPILOG
  // ========================================================================
  L(*epilog_label_);
  epilog_label_ = nullptr;
  EmitTraceUserCallReturn();
  LDR(GetContextReg(), SP, StackLayout::GUEST_CTX_HOME);
  PopStackpoint();

  // Pop stackpoint before leaving.
  PopStackpoint();

  // Restore host return address and deallocate stack.
  ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
  if (stack_size <= 4095) {
    add(sp, sp, static_cast<uint32_t>(stack_size));
  } else {
    mov(x17, static_cast<uint64_t>(stack_size));
    add(sp, sp, x17, UXTX);
  }
  ret();

  // ========================================================================
  // TAIL CODE
  // ========================================================================
  for (auto& tail_item : tail_code_) {
    // ARM64 instructions are always 4-byte aligned, so alignment is mostly
    // a no-op unless we want cache-line alignment for hot paths.
    L(tail_item.label);
    tail_item.func(*this, tail_item.label);
  }
  code_offsets.tail = getSize();

  // Fill in EmitFunctionInfo metrics.
  assert_zero(code_offsets.prolog);
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;

  return true;
}

void* A64Emitter::Emplace(const EmitFunctionInfo& func_info,
                          GuestFunction* function) {
  assert_true(func_info.code_size.total == getSize());

  void* new_execute_address;
  void* new_write_address;

  if (function) {
    code_cache_->PlaceGuestCode(
        function->address(),
        const_cast<void*>(static_cast<const void*>(getCode())), func_info,
        function, new_execute_address, new_write_address);
  } else {
    code_cache_->PlaceHostCode(
        0, const_cast<void*>(static_cast<const void*>(getCode())), func_info,
        new_execute_address, new_write_address);
  }

  // In xbyak_aarch64, labels are resolved at define time (backpatching),
  // so all relative offsets are already correct. We just need to reset
  // the codegen state for the next function.
  reset();
  tail_code_.clear();

  // Clean up cached labels.
  for (auto* cached_label : label_cache_) {
    delete cached_label;
  }
  label_cache_.clear();

  // Clean up HIR->xbyak label map.
  for (auto& pair : label_map_) {
    delete pair.second;
  }
  label_map_.clear();

  return new_execute_address;
}

void A64Emitter::MarkSourceOffset(const hir::Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->guest_address = static_cast<uint32_t>(i->src1.offset);
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = static_cast<uint32_t>(getSize());
}

void A64Emitter::DebugBreak() { brk(0xF000); }

void A64Emitter::EmitTraceUserCallReturn() {}

void A64Emitter::DebugBreak() { BRK(0xF000); }

void A64Emitter::HandleStackpointOverflowError(ppc::PPCContext* context) {
  if (debugging::IsDebuggerAttached()) {
    debugging::Break();
  }
  xe::FatalError(
      "Overflowed stackpoints! Please report this error for this title to "
      "Xenia developers.");
}

uint64_t TrapDebugPrint(void* raw_context, uint64_t address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);
  uint32_t str_ptr = uint32_t(thread_state->context()->r[3]);
  // uint16_t str_len = uint16_t(thread_state->context()->r[4]);
  auto str = thread_state->memory()->TranslateVirtual<const char*>(str_ptr);
  // TODO(benvanik): truncate to length?
  XELOGD("(DebugPrint) {}", str);

  if (cvars::debugprint_trap_log) {
    debugging::DebugPrint("(DebugPrint) {}", str);
  }

  return 0;
}

uint64_t TrapLogRegs(void* raw_context, uint64_t address) {
  static volatile int32_t log_count = 0;
  if (xe::atomic_inc(&log_count) > 8) {
    return 0;
  }
  auto guest_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  if (!guest_context) {
    return 0;
  }
  auto thread_state = guest_context->thread_state;
  XELOGI(
      "TraceOnInstruction 0x{:08X}: r3=0x{:016X} r4=0x{:016X} r9=0x{:016X} "
      "r10=0x{:016X} r11=0x{:016X} r30=0x{:016X} r31=0x{:016X} lr=0x{:016X} "
      "ctr=0x{:016X} cr=0x{:08X} xer=0x{:08X}",
      static_cast<uint32_t>(cvars::break_on_instruction), guest_context->r[3],
      guest_context->r[4], guest_context->r[9], guest_context->r[10],
      guest_context->r[11], guest_context->r[30], guest_context->r[31],
      guest_context->lr, guest_context->ctr,
      static_cast<uint32_t>(guest_context->cr()),
      static_cast<uint32_t>((uint32_t(guest_context->xer_so & 1) << 31) |
                            (uint32_t(guest_context->xer_ov & 1) << 30) |
                            (uint32_t(guest_context->xer_ca & 1) << 29)));
  if (thread_state) {
    auto memory = thread_state->memory();
    if (memory) {
      auto page_access_to_string = [](xe::memory::PageAccess access) {
        switch (access) {
          case xe::memory::PageAccess::kNoAccess:
            return "no-access";
          case xe::memory::PageAccess::kReadOnly:
            return "read-only";
          case xe::memory::PageAccess::kReadWrite:
            return "read-write";
          case xe::memory::PageAccess::kExecuteReadOnly:
            return "exec-read";
          case xe::memory::PageAccess::kExecuteReadWrite:
            return "exec-read-write";
        }
        return "unknown";
      };
      auto heap_type_to_string = [](HeapType type) {
        switch (type) {
          case HeapType::kGuestVirtual:
            return "guest-virtual";
          case HeapType::kGuestXex:
            return "guest-xex";
          case HeapType::kGuestPhysical:
            return "guest-physical";
          case HeapType::kHostPhysical:
            return "host-physical";
        }
        return "unknown";
      };
      auto can_read_guest = [&](uint32_t addr) -> bool {
        if (!addr) {
          return false;
        }
        auto* heap = memory->LookupHeap(addr);
        if (!heap) {
          return false;
        }
        return heap->QueryRangeAccess(addr, addr) !=
               xe::memory::PageAccess::kNoAccess;
      };
      auto log_guest_bytes = [&](uint32_t addr, const char* label) {
        if (!can_read_guest(addr)) {
          auto* heap = memory->LookupHeap(addr);
          XELOGI(
              "TraceOnInstruction {}: addr=0x{:08X} unreadable heap={} "
              "access={}",
              label, addr,
              heap ? heap_type_to_string(heap->heap_type()) : "none",
              heap ? page_access_to_string(heap->QueryRangeAccess(addr, addr))
                   : "no-access");
          return;
        }
        const auto* heap = memory->LookupHeap(addr);
        const uint8_t* host_ptr = nullptr;
        if (heap && heap->heap_type() == HeapType::kGuestPhysical) {
          uint32_t physical_address = memory->GetPhysicalAddress(addr);
          host_ptr = memory->TranslatePhysical(physical_address);
        } else {
          host_ptr = memory->TranslateVirtual<const uint8_t*>(addr);
        }
        if (!host_ptr) {
          XELOGI("TraceOnInstruction {}: addr=0x{:08X} null", label, addr);
          return;
        }
        uint8_t bytes[16] = {};
        std::memcpy(bytes, host_ptr, sizeof(bytes));
        char ascii[sizeof(bytes) + 1] = {};
        for (size_t i = 0; i < sizeof(bytes); ++i) {
          uint8_t ch = bytes[i];
          ascii[i] = (ch >= 0x20 && ch <= 0x7E) ? static_cast<char>(ch) : '.';
        }
        XELOGI(
            "TraceOnInstruction {}: addr=0x{:08X} {:02X} {:02X} {:02X} "
            "{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
            "{:02X} {:02X} {:02X} {:02X} {:02X} ascii={}",
            label, addr, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
            bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
            bytes[11], bytes[12], bytes[13], bytes[14], bytes[15], ascii);
      };
      auto log_guest_string = [&](uint32_t addr, const char* label) {
        if (!can_read_guest(addr)) {
          return;
        }
        const uint8_t* ptr = memory->TranslateVirtual<const uint8_t*>(addr);
        if (!ptr) {
          return;
        }
        char buffer[129] = {};
        size_t len = 0;
        for (; len < sizeof(buffer) - 1; ++len) {
          char ch = static_cast<char>(ptr[len]);
          if (!ch) {
            break;
          }
          if (!std::isprint(static_cast<unsigned char>(ch))) {
            return;
          }
          buffer[len] = ch;
        }
        if (len > 0) {
          XELOGI("TraceOnInstruction {}: {}", label, buffer);
        }
      };
      const uint32_t guest_address = static_cast<uint32_t>(guest_context->r[4]);
      const auto* heap = memory->LookupHeap(guest_address);
      if (heap) {
        const uint8_t* host_ptr = nullptr;
        if (heap->heap_type() == HeapType::kGuestPhysical) {
          uint32_t physical_address = memory->GetPhysicalAddress(guest_address);
          host_ptr = memory->TranslatePhysical(physical_address);
        } else {
          host_ptr = memory->TranslateVirtual<const uint8_t*>(guest_address);
        }
        if (host_ptr) {
          uint8_t bytes[16] = {};
          std::memcpy(bytes, host_ptr, sizeof(bytes));
          char ascii[sizeof(bytes) + 1] = {};
          for (size_t i = 0; i < sizeof(bytes); ++i) {
            uint8_t ch = bytes[i];
            ascii[i] = (ch >= 0x20 && ch <= 0x7E) ? static_cast<char>(ch) : '.';
          }
          XELOGI(
              "TraceOnInstruction mem[r4]=0x{:08X}: {:02X} {:02X} {:02X} "
              "{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
              "{:02X} {:02X} {:02X} {:02X} {:02X} ascii={}",
              guest_address, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
              bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
              bytes[11], bytes[12], bytes[13], bytes[14], bytes[15], ascii);
        }
      }

      auto read_u32 = [&](uint32_t addr, uint32_t* out) -> bool {
        if (!can_read_guest(addr)) {
          return false;
        }
        const auto* heap = memory->LookupHeap(addr);
        if (heap->heap_type() == HeapType::kGuestPhysical) {
          uint32_t physical_address = memory->GetPhysicalAddress(addr);
          auto ptr =
              memory->TranslatePhysical<const uint32_t*>(physical_address);
          if (!ptr) {
            return false;
          }
          *out = xe::load_and_swap<uint32_t>(ptr);
          return true;
        }
        auto ptr = memory->TranslateVirtual<const uint32_t*>(addr);
        if (!ptr) {
          return false;
        }
        *out = xe::load_and_swap<uint32_t>(ptr);
        return true;
      };
      const uint32_t trace_pc =
          static_cast<uint32_t>(cvars::break_on_instruction);
      auto log_trace_instr = [&](uint32_t pc, const char* label) {
        if (!can_read_guest(pc)) {
          auto* heap = memory->LookupHeap(pc);
          XELOGI(
              "TraceOnInstruction {}: pc=0x{:08X} unreadable heap={} "
              "access={}",
              label, pc, heap ? heap_type_to_string(heap->heap_type()) : "none",
              heap ? page_access_to_string(heap->QueryRangeAccess(pc, pc))
                   : "no-access");
          return;
        }
        uint32_t instr = 0;
        if (!read_u32(pc, &instr)) {
          XELOGI("TraceOnInstruction {}: pc=0x{:08X} unreadable", label, pc);
          return;
        }
        xe::StringBuffer disasm;
        if (cpu::ppc::DisasmPPC(pc, instr, &disasm)) {
          XELOGI("TraceOnInstruction {}: pc=0x{:08X} instr=0x{:08X} {}", label,
                 pc, instr, disasm.to_string_view());
        } else {
          XELOGI("TraceOnInstruction {}: pc=0x{:08X} instr=0x{:08X}", label, pc,
                 instr);
        }
      };
      if (trace_pc) {
        log_trace_instr(trace_pc - 4, "target-4");
        log_trace_instr(trace_pc, "target");
        log_trace_instr(trace_pc + 4, "target+4");
      }
      if (memory->LookupHeap(trace_pc)) {
        for (int offset = -4; offset <= 4; ++offset) {
          uint32_t pc = trace_pc + offset * 4;
          if (!memory->LookupHeap(pc)) {
            continue;
          }
          uint32_t instr = 0;
          if (!read_u32(pc, &instr)) {
            XELOGI("TraceOnInstruction window: pc=0x{:08X} unreadable", pc);
            continue;
          }
          xe::StringBuffer disasm_window;
          if (cpu::ppc::DisasmPPC(pc, instr, &disasm_window)) {
            XELOGI("TraceOnInstruction window: pc=0x{:08X} instr=0x{:08X} {}",
                   pc, instr, disasm_window.to_string_view());
          } else {
            XELOGI("TraceOnInstruction window: pc=0x{:08X} instr=0x{:08X}", pc,
                   instr);
          }
        }
      }

      const uint32_t obj_address = static_cast<uint32_t>(guest_context->r[3]);
      if (!obj_address) {
        XELOGI("TraceOnInstruction r3 fields: base=0x00000000");
      } else {
        const auto* obj_heap = memory->LookupHeap(obj_address);
        if (obj_heap) {
          uint32_t value_4 = 0;
          uint32_t value_8 = 0;
          uint32_t value_c = 0;
          uint32_t value_20 = 0;
          uint32_t value_470 = 0;
          bool have_any = false;
          have_any |= read_u32(obj_address + 0x4, &value_4);
          have_any |= read_u32(obj_address + 0x8, &value_8);
          have_any |= read_u32(obj_address + 0xC, &value_c);
          have_any |= read_u32(obj_address + 0x20, &value_20);
          have_any |= read_u32(obj_address + 0x470, &value_470);
          if (have_any) {
            XELOGI(
                "TraceOnInstruction r3 fields: base=0x{:08X} +0x4=0x{:08X} "
                "+0x8=0x{:08X} +0xC=0x{:08X} +0x20=0x{:08X} +0x470=0x{:08X}",
                obj_address, value_4, value_8, value_c, value_20, value_470);
            if (value_20) {
              log_guest_bytes(value_20, "r3+0x20");
            }
            if (value_470) {
              log_guest_bytes(value_470, "r3+0x470");
            }
          } else {
            XELOGI("TraceOnInstruction r3 fields: base=0x{:08X} unmapped",
                   obj_address);
          }
        } else {
          XELOGI("TraceOnInstruction r3 fields: base=0x{:08X} heap=null",
                 obj_address);
        }
        log_guest_string(obj_address, "r3 string");
      }

      const uint32_t r31_address = static_cast<uint32_t>(guest_context->r[31]);
      if (!r31_address) {
        XELOGI("TraceOnInstruction r31 fields: base=0x00000000");
      } else {
        const auto* r31_heap = memory->LookupHeap(r31_address);
        if (r31_heap) {
          uint32_t value_4 = 0;
          uint32_t value_8 = 0;
          uint32_t value_c = 0;
          uint32_t value_20 = 0;
          uint32_t value_470 = 0;
          bool have_any = false;
          have_any |= read_u32(r31_address + 0x4, &value_4);
          have_any |= read_u32(r31_address + 0x8, &value_8);
          have_any |= read_u32(r31_address + 0xC, &value_c);
          have_any |= read_u32(r31_address + 0x20, &value_20);
          have_any |= read_u32(r31_address + 0x470, &value_470);
          if (have_any) {
            XELOGI(
                "TraceOnInstruction r31 fields: base=0x{:08X} +0x4=0x{:08X} "
                "+0x8=0x{:08X} +0xC=0x{:08X} +0x20=0x{:08X} +0x470=0x{:08X}",
                r31_address, value_4, value_8, value_c, value_20, value_470);
            if (value_20) {
              log_guest_bytes(value_20, "r31+0x20");
            }
            if (value_470) {
              log_guest_bytes(value_470, "r31+0x470");
            }
          } else {
            XELOGI("TraceOnInstruction r31 fields: base=0x{:08X} unmapped",
                   r31_address);
          }
        } else {
          XELOGI("TraceOnInstruction r31 fields: base=0x{:08X} heap=null",
                 r31_address);
        }
      }
    }
  }
  return 0;
}

uint64_t TrapDebugBreak(void* raw_context, uint64_t address) {
  [[maybe_unused]] auto thread_state =
      *reinterpret_cast<ThreadState**>(raw_context);
  if (cvars::break_on_debugbreak) {
    xe::debugging::Break();
  }
  if (cvars::a64_fail_fast_on_trap) {
    static std::atomic<bool> exiting_on_trap{false};
    if (!exiting_on_trap.exchange(true, std::memory_order_relaxed)) {
      XELOGE("A64 forced trap hit at guest address 0x{:08X} - terminating",
             static_cast<uint32_t>(address));
      FatalError(
          "A64 forced trap hit. Exiting immediately to prevent repeated trap "
          "log spam.");
    }
    std::_Exit(EXIT_FAILURE);
  }
  XELOGE("tw/td forced trap hit! This should be a crash!");
  return 0;
}

static void CheckStackSync(void* raw_context, uint64_t guest_pc) {
  if (!cvars::a64_stack_sync_check || !raw_context) {
    return;
  }
  auto* context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  auto* processor = context->processor;
  if (!processor) {
    return;
  }
  auto* backend = static_cast<A64Backend*>(processor->backend());
  if (!backend) {
    return;
  }
  auto* backend_context = backend->BackendContextForGuestContext(context);
  if (!backend_context || !backend_context->stackpoints ||
      backend_context->current_stackpoint_depth == 0) {
    return;
  }
  const uint32_t depth = backend_context->current_stackpoint_depth;
  const auto& top = backend_context->stackpoints[depth - 1];
  const uint32_t guest_sp = static_cast<uint32_t>(context->r[1]);
  if (guest_sp <= top.guest_sp) {
    return;
  }
  static std::atomic<int32_t> log_count{0};
  const int32_t count = log_count.fetch_add(1, std::memory_order_relaxed);
  if (count < 64) {
    XELOGW(
        "A64 stack check: guest_sp grew (pc=0x{:08X} sp=0x{:08X} "
        "top_sp=0x{:08X} depth={} lr=0x{:08X})",
        static_cast<uint32_t>(guest_pc), guest_sp, top.guest_sp, depth,
        static_cast<uint32_t>(context->lr));
  }
}

void A64Emitter::Trap(uint16_t trap_type) {
  switch (trap_type) {
    case 20:
    case 26:
      // 0x0FE00014 is a 'debug print' where r3 = buffer r4 = length
      CallNative(TrapDebugPrint, 0);
      break;
    case 27:
      CallNative(TrapLogRegs, 0);
      break;
    case 0:
    case 22:
      // Always trap?
      // TODO(benvanik): post software interrupt to debugger.
      CallNative(TrapDebugBreak, 0);
      break;
    case 25:
      // ?
      break;
    default:
      if (cvars::a64_fail_fast_on_trap) {
        FatalError(fmt::format(
            "Unknown A64 trap type {}. Exiting to avoid repeated failures.",
            trap_type));
      }
      XELOGW("Unknown trap type {}", trap_type);
      BRK(0xF000);
      break;
  }
}

void A64Emitter::UnimplementedInstr(const hir::Instr* i) {
  // TODO(benvanik): notify debugger.
  BRK(0xF000);
  assert_always();
}

// This is used by the A64ThunkEmitter's ResolveFunctionThunk.
uint64_t ResolveFunction(void* raw_context, uint64_t target_address) {
  auto guest_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  assert_not_null(guest_context);
  auto thread_state = guest_context->thread_state;
  assert_not_null(thread_state);

  assert_not_zero(target_address);

  uint32_t guest_address = 0;
  if (target_address > 0xFFFFFFFF) {
    auto ctx_ptr = reinterpret_cast<uint64_t>(guest_context);
    if (target_address >= ctx_ptr &&
        target_address < ctx_ptr + sizeof(ppc::PPCContext)) {
      XELOGE(
          "ResolveFunction: target_address 0x{:016X} is within PPCContext "
          "[0x{:016X}, 0x{:016X})",
          target_address, ctx_ptr, ctx_ptr + sizeof(ppc::PPCContext));
      XELOGE(
          "ResolveFunction: The target register contains a context pointer "
          "instead of a function address");
      return 0;
    }

    auto code_cache = static_cast<A64CodeCache*>(
        thread_state->processor()->backend()->code_cache());
    auto guest_function = code_cache->LookupFunction(target_address);
    if (guest_function) {
      guest_address =
          guest_function->MapMachineCodeToGuestAddress(target_address);
    } else {
      guest_address = static_cast<uint32_t>(target_address);
    }
  } else {
    guest_address = static_cast<uint32_t>(target_address);
  }

  if (guest_address == 0) {
    XELOGE("ResolveFunction: guest_address is 0! This should not happen");
    return 0;
  }

  if (cvars::a64_enable_host_guest_stack_synchronization &&
      target_address <= 0xFFFFFFFFu) {
    auto processor = thread_state->processor();
    auto module_for_address =
        processor->LookupModule(static_cast<uint32_t>(target_address));
    if (module_for_address) {
      auto* xexmod = dynamic_cast<XexModule*>(module_for_address);
      if (xexmod) {
        InfoCacheFlags* flags = xexmod->GetInstructionAddressFlags(
            static_cast<uint32_t>(target_address));
        if (flags && flags->is_return_site) {
          auto ones_with_address = processor->FindFunctionsWithAddress(
              static_cast<uint32_t>(target_address));
          if (!ones_with_address.empty()) {
            A64Function* candidate = nullptr;
            uintptr_t host_address = 0;
            for (auto&& entry : ones_with_address) {
              auto* afunc = static_cast<A64Function*>(entry);
              host_address = afunc->MapGuestAddressToMachineCode(
                  static_cast<uint32_t>(target_address));
              if (host_address &&
                  afunc->machine_code() !=
                      reinterpret_cast<const uint8_t*>(host_address)) {
                candidate = afunc;
                break;
              }
            }

            if (candidate && host_address) {
              auto* backend = static_cast<A64Backend*>(processor->backend());
              auto* backend_context =
                  backend->BackendContextForGuestContext(guest_context);
              if (backend_context->stackpoints &&
                  backend_context->current_stackpoint_depth > 0) {
                uint32_t current_stackpoint_index =
                    backend_context->current_stackpoint_depth - 1;
                uint32_t current_guest_stackpointer =
                    static_cast<uint32_t>(guest_context->r[1]);
                uint32_t num_frames_bigger = 0;

                while (current_stackpoint_index != 0xFFFFFFFF) {
                  if (current_guest_stackpointer >
                      backend_context->stackpoints[current_stackpoint_index]
                          .guest_sp) {
                    --current_stackpoint_index;
                    ++num_frames_bigger;
                  } else {
                    break;
                  }
                }

                if (num_frames_bigger > 1 &&
                    current_stackpoint_index != 0xFFFFFFFF) {
                  // Try to match the guest LR to disambiguate same-guest-sp
                  // frames.
                  uint32_t guest_lr = static_cast<uint32_t>(guest_context->lr);
                  uint32_t scan_index = current_stackpoint_index;
                  while (scan_index != 0xFFFFFFFF) {
                    const auto& sp_entry =
                        backend_context->stackpoints[scan_index];
                    if (sp_entry.guest_sp != current_guest_stackpointer) {
                      break;
                    }
                    if (sp_entry.guest_return_address == guest_lr) {
                      current_stackpoint_index = scan_index;
                      break;
                    }
                    if (scan_index == 0) {
                      break;
                    }
                    --scan_index;
                  }

                  if (cvars::a64_stack_sync_log) {
                    static std::atomic<int32_t> sync_log_count{0};
                    const int32_t limit = cvars::a64_stack_sync_log_limit;
                    const int32_t count =
                        sync_log_count.fetch_add(1, std::memory_order_relaxed);
                    if (limit <= 0 || count < limit) {
                      XELOGI(
                          "A64 stack sync: guest=0x{:08X} host=0x{:016X} "
                          "guest_sp=0x{:08X} depth={} index={}",
                          static_cast<uint32_t>(target_address), host_address,
                          current_guest_stackpointer,
                          backend_context->current_stackpoint_depth,
                          current_stackpoint_index);
                    }
                  }
                  return host_address;
                }
              }
            }
          }
        }
      }
    }
  }

  auto fn = thread_state->processor()->ResolveFunction(guest_address);
  if (!fn) {
    XELOGE(
        "ResolveFunction: Failed to resolve function at guest address 0x{:08X}",
        guest_address);
    XELOGE("ResolveFunction: Original target_address was 0x{:016X}",
           target_address);
    if (ShouldLogResolveFailure()) {
      const uint32_t lr_guest = static_cast<uint32_t>(guest_context->lr);
      XELOGI(
          "ResolveFunction: lr=0x{:016X} ctr=0x{:016X} thread_id={} "
          "target_is_host={} guest_address=0x{:08X}",
          guest_context->lr, guest_context->ctr, guest_context->thread_id,
          target_address > 0xFFFFFFFF, guest_address);
      auto log_modules_for_address = [&](uint32_t address, const char* label) {
        bool found = false;
        for (auto* module : thread_state->processor()->GetModules()) {
          if (!module) {
            continue;
          }
          if (module->ContainsAddress(address)) {
            XELOGI("ResolveFunction: {} module '{}' contains 0x{:08X}", label,
                   module->name(), address);
            found = true;
          }
        }
        if (!found) {
          XELOGI("ResolveFunction: {} no module contains 0x{:08X}", label,
                 address);
        }
      };
      log_modules_for_address(lr_guest, "lr");
      log_modules_for_address(guest_address, "guest");

      auto lr_functions =
          thread_state->processor()->FindFunctionsWithAddress(lr_guest);
      if (lr_functions.empty()) {
        XELOGI("ResolveFunction: no resolved function covers LR 0x{:08X}",
               lr_guest);
      } else {
        const auto* fn = lr_functions.front();
        XELOGI("ResolveFunction: LR function {} [0x{:08X},0x{:08X}) name='{}'",
               lr_functions.size(), fn->address(), fn->end_address(),
               fn->name());
      }

      auto* memory = thread_state->memory();
      if (!memory) {
        XELOGI("ResolveFunction: no Memory available for guest dump");
      } else if (!memory->LookupHeap(guest_address)) {
        XELOGI(
            "ResolveFunction: guest_address 0x{:08X} not in any heap for dump",
            guest_address);
      } else {
        const uint8_t* data =
            memory->TranslateVirtual<const uint8_t*>(guest_address);
        std::array<uint8_t, 16> bytes = {};
        std::memcpy(bytes.data(), data, bytes.size());
        XELOGI(
            "ResolveFunction: guest[0x{:08X}] = {:02X} {:02X} {:02X} {:02X} "
            "{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
            "{:02X} {:02X} {:02X} {:02X}",
            guest_address, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
            bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
            bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
      }
    }
    return 0;
  }

  auto a64_fn = static_cast<A64Function*>(fn);
  if (!a64_fn->machine_code()) {
    XELOGE(
        "ResolveFunction: Function at guest address 0x{:08X} has no machine "
        "code",
        guest_address);
    return 0;
  }

  return reinterpret_cast<uint64_t>(a64_fn->machine_code());
}

void A64Emitter::Call(const hir::Instr* instr, GuestFunction* function) {
  assert_not_null(function);
  ForgetFpcrMode();
  auto fn = static_cast<A64Function*>(function);

  if (fn->machine_code()) {
    // TODO(benvanik): is it worth it to do this? It removes the need for
    // a ResolveFunction call, but makes the table less useful.
#if XE_A64_INDIRECTION_64BIT
    MOV(X16, reinterpret_cast<uint64_t>(fn->machine_code()));
#else
    assert_zero(uint64_t(fn->machine_code()) & 0xFFFFFFFF00000000);
    MOV(X16, uint32_t(uint64_t(fn->machine_code())));
#endif
  } else if (code_cache_->has_indirection_table()) {
    // Load the pointer to the indirection table maintained in A64CodeCache.
    // The target dword will either contain the address of the generated code
    // or a thunk to ResolveAddress.
    MOV(W17, function->address());
#if XE_A64_INDIRECTION_64BIT
    // ARM64 indirection table stores rel32 code-cache offsets and tagged
    // external targets.
    oaknut::Label external_target;
    oaknut::Label indirection_target_ready;
    MOV(X14, code_cache_->indirection_table_base_bias());
    ADD(X14, X14, W17, UXTW);
    LDR(W16, X14);
    CMP(W16, 0);
    B(oaknut::Cond::LT, external_target);
    MOV(X14, code_cache_->execute_base_address());
    ADD(X16, X14, W16, UXTW);
    B(indirection_target_ready);
    l(external_target);
    AND(W15, W16, A64CodeCache::kIndirectionExternalIndexMask);
    MOV(X14, code_cache_->external_indirection_table_base_address());
    LSL(X15, X15, 3);
    ADD(X14, X14, X15);
    LDR(X16, X14);
    l(indirection_target_ready);
#else
    // Other platforms use 32-bit addresses mapped at guest address space.
    if (code_cache_->indirection_table_base_address() ==
        A64CodeCache::kIndirectionTableBase) {
      LDR(W16, X17);
    } else {
      // Tail call: pass our return address to the callee.
      PopStackpoint();
      ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
      ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
      if (stack_size() <= 4095) {
        add(sp, sp, static_cast<uint32_t>(stack_size()));
      } else {
        mov(x17, static_cast<uint64_t>(stack_size()));
        add(sp, sp, x17, UXTX);
      }
      br(x9);
    }
    return;
  }

  // Actually jump/call to X16.
  if (instr->flags & hir::CALL_TAIL) {
    // Since we skip the prolog we need to mark the return here.
    EmitTraceUserCallReturn();

    // Pass the callers return address over.
    LDR(X0, SP, StackLayout::GUEST_RET_ADDR);

    PopStackpoint();
    AdjustStackPointer(*this, stack_size(), true);

    MOV(SP, X29);
    LDP(X29, X30, SP, POST_INDEXED, 16);

    BR(X16);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    LDR(X0, SP, StackLayout::GUEST_CALL_RET_ADDR);

    BLR(X16);

    EnsureSynchronizedGuestAndHostStack();

    if (cvars::a64_stack_sync_check) {
      MOV(GetNativeParam(0), instr->GuestAddressFor());
      CallNativeSafe(reinterpret_cast<void*>(CheckStackSync));
    }
  }
}

void A64Emitter::CallIndirect(const hir::Instr* instr,
                              const oaknut::XReg& reg) {
  // Check if return.
  if (instr->flags & hir::CALL_POSSIBLE_RETURN) {
    LDR(W16, SP, StackLayout::GUEST_RET_ADDR);
    CMP(reg.toW(), W16);
    B(oaknut::Cond::EQ, epilog_label());
  }

  // Load the pointer to the indirection table maintained in A64CodeCache.
  // The target dword will either contain the address of the generated code
  // or a thunk to ResolveAddress.
  if (code_cache_->has_indirection_table()) {
    if (reg.toW().index() != W17.index()) {
      MOV(W17, reg.toW());
    }
#if XE_A64_INDIRECTION_64BIT
    // ARM64 indirection table stores rel32 code-cache offsets and tagged
    // external targets.
    oaknut::Label external_target;
    oaknut::Label indirection_target_ready;
    MOV(X14, code_cache_->indirection_table_base_bias());
    ADD(X14, X14, W17, UXTW);
    LDR(W16, X14);
    CMP(W16, 0);
    B(oaknut::Cond::LT, external_target);
    MOV(X14, code_cache_->execute_base_address());
    ADD(X16, X14, W16, UXTW);
    B(indirection_target_ready);
    l(external_target);
    AND(W15, W16, A64CodeCache::kIndirectionExternalIndexMask);
    MOV(X14, code_cache_->external_indirection_table_base_address());
    LSL(X15, X15, 3);
    ADD(X14, X14, X15);
    LDR(X16, X14);
    l(indirection_target_ready);
#else
    // Other platforms use 32-bit addresses mapped at guest address space.
    if (code_cache_->indirection_table_base_address() ==
        A64CodeCache::kIndirectionTableBase) {
      LDR(W16, X17);
    } else {
      MOV(W16, static_cast<uint32_t>(A64CodeCache::kIndirectionTableBase));
      SUB(W17, W17, W16);
      MOV(X16, code_cache_->indirection_table_base_address());
      ADD(X16, X16, W17, UXTW);
      LDR(W16, X16);
    }
#endif
  } else {
    // Old-style resolve.
    // Not too important because indirection table is almost always available.
    MOV(W1, reg.toW());
    CallNativeSafe(reinterpret_cast<void*>(ResolveFunction));
    ReloadMembase();
    MOV(X16, X0);
  }

  if (instr->flags & hir::CALL_TAIL) {
    // Since we skip the prolog we need to mark the return here.
    EmitTraceUserCallReturn();

    // Pass the callers return address over.
    LDR(X0, SP, StackLayout::GUEST_RET_ADDR);

    PopStackpoint();
    AdjustStackPointer(*this, stack_size(), true);

    MOV(SP, X29);
    LDP(X29, X30, SP, POST_INDEXED, 16);

    BR(X16);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    LDR(X0, SP, StackLayout::GUEST_CALL_RET_ADDR);

    BLR(X16);

    EnsureSynchronizedGuestAndHostStack();

    if (cvars::a64_stack_sync_check) {
      MOV(GetNativeParam(0), instr->GuestAddressFor());
      CallNativeSafe(reinterpret_cast<void*>(CheckStackSync));
    }
  }
}

uint64_t UndefinedCallExtern(void* raw_context, uint64_t function_ptr) {
  auto function = reinterpret_cast<Function*>(function_ptr);
  if (cvars::log_undefined_extern_args &&
      function->name() == "XeKeysConsolePrivateKeySign") {
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
      auto* context = reinterpret_cast<ppc::PPCContext*>(raw_context);
      XELOGI(
          "Undefined extern {} args: r3={:016X} r4={:016X} r5={:016X} "
          "r6={:016X} r7={:016X} r8={:016X} r9={:016X} r10={:016X}",
          function->name(), context->r[3], context->r[4], context->r[5],
          context->r[6], context->r[7], context->r[8], context->r[9],
          context->r[10]);
    }
    br(x9);
  } else {
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
    blr(x9);
    synchronize_stack_on_next_instruction_ = true;
  }
}

void A64Emitter::CallIndirect(const hir::Instr* instr, int reg_index) {
  ForgetFpcrMode();
  auto target_w = WReg(reg_index);

  // Check if this is a possible return (e.g., PPC blr).
  if (instr->flags & hir::CALL_POSSIBLE_RETURN) {
    // Compare target guest address with our function's return address.
    ldr(w0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    cmp(target_w, w0);
    b(EQ, epilog_label());
  }

  // Load host code address from indirection table.
  if (code_cache_->has_indirection_table()) {
    // Must leave the guest address in w16 for the resolve thunk to read.
    if (target_w.getIdx() != w16.getIdx()) {
      mov(w16, target_w);
    }
    if (!code_cache_->encoded_indirection()) {
      // Fast path: table mapped at host VA == guest addr; slot holds raw
      // 32-bit host target.
      ldr(w9, ptr(x16, static_cast<uint32_t>(0)));
    } else {
      // Encoded path: see A64CodeCache for the entry format.
      Label external_target;
      Label indirection_ready;

      mov(x14, code_cache_->indirection_table_base_bias());
      add(x14, x14, w16, UXTW);
      ldr(w9, ptr(x14, static_cast<uint32_t>(0)));
      tbnz(w9, 31, external_target);

      // Internal: rel32 from code cache base.
      mov(x14, code_cache_->execute_base_address());
      add(x9, x14, w9, UXTW);
      b(indirection_ready);

      // External: tagged index into the side table.
      L(external_target);
      and_(w15, w9, A64CodeCache::kIndirectionExternalIndexMask);
      mov(x14, code_cache_->external_indirection_table_base_address());
      lsl(x15, x15, 3);
      add(x14, x14, x15);
      ldr(x9, ptr(x14, static_cast<uint32_t>(0)));

      L(indirection_ready);
    }
  } else {
    // No indirection table: resolve at runtime.
    mov(w16, target_w);
    mov(x0, x20);  // context
    mov(x1, x16);  // guest address
    mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
    blr(x9);
    mov(x9, x0);  // resolved address
  }

  if (instr->flags & hir::CALL_TAIL) {
    // Tail call: pass our return address to the callee.
    PopStackpoint();
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
    if (stack_size() <= 4095) {
      add(sp, sp, static_cast<uint32_t>(stack_size()));
    } else {
      mov(x17, static_cast<uint64_t>(stack_size()));
      add(sp, sp, x17, UXTX);
    }
    br(x9);
  } else {
    // Regular call: pass the next call's return address.
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
    blr(x9);
    synchronize_stack_on_next_instruction_ = true;
  }
}

void A64Emitter::CallExtern(const hir::Instr* instr, const Function* function) {
  ForgetFpcrMode();
  bool undefined = true;
  if (function->behavior() == Function::Behavior::kBuiltin) {
    auto builtin_function = static_cast<const BuiltinFunction*>(function);
    if (builtin_function->handler()) {
      undefined = false;
      // GuestToHostThunk: x0=target, x1=arg0, x2=arg1
      // Thunk rearranges to: x0=context, x1=arg0, x2=arg1, calls target
      mov(x0, reinterpret_cast<uint64_t>(builtin_function->handler()));
      mov(x1, reinterpret_cast<uint64_t>(builtin_function->arg0()));
      mov(x2, reinterpret_cast<uint64_t>(builtin_function->arg1()));
      mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
      blr(x9);
    }
  } else if (function->behavior() == Function::Behavior::kExtern) {
    auto extern_function = static_cast<const GuestFunction*>(function);
    if (extern_function->extern_handler()) {
      undefined = false;
      // GuestToHostThunk: x0=target, x1=arg0
      mov(x0, reinterpret_cast<uint64_t>(extern_function->extern_handler()));
      ldr(x1, ptr(GetContextReg(), static_cast<int32_t>(offsetof(
                                       ppc::PPCContext, kernel_state))));
      mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
      blr(x9);
    }
  }
  if (undefined) {
    // Set arg0 = function pointer, then call UndefinedCallExtern via thunk.
    mov(x1, reinterpret_cast<uint64_t>(function));
    CallNativeSafe(reinterpret_cast<void*>(&UndefinedCallExtern));
  }
}

void A64Emitter::CallNative(void* fn) { CallNativeSafe(fn); }

void A64Emitter::CallNativeSafe(void* fn) {
  // GuestToHostThunk: x0=target function, x1/x2=args (set by caller).
  // The thunk rearranges: saves x0 in x9, sets x0=context, calls x9.
  mov(x0, reinterpret_cast<uint64_t>(fn));
  mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
  blr(x9);
}

void A64Emitter::SetReturnAddress(uint64_t value) {
  mov(x0, value);
  str(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
}

void A64Emitter::ReloadMembase() {
  // Reload x21 from context->virtual_membase.
  ldr(x21, ptr(x20, static_cast<int32_t>(
                        offsetof(ppc::PPCContext, virtual_membase))));
}

void A64Emitter::PushStackpoint() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  oaknut::Label done;
  oaknut::Label overflowed;
  // backend_ctx = context - sizeof(A64BackendContext)
  SUB(X9, GetContextReg(), sizeof(A64BackendContext));
  LDR(X10, X9, offsetof(A64BackendContext, stackpoints));
  CBZ(X10, done);

  LDR(W11, X9, offsetof(A64BackendContext, current_stackpoint_depth));
  MOV(W12, static_cast<uint32_t>(cvars::max_stackpoints));
  CMP(W11, W12);
  B(oaknut::Cond::HS, overflowed);

  // entry = stackpoints + (depth * sizeof(A64BackendStackpoint))
  LSL(X12, X11, 5);  // sizeof(A64BackendStackpoint) == 32
  ADD(X10, X10, X12);

  MOV(X13, SP);
  STR(X13, X10, offsetof(A64BackendStackpoint, host_sp));
  STR(X29, X10, offsetof(A64BackendStackpoint, host_fp));
  LDR(W13, GetContextReg(), offsetof(ppc::PPCContext, r[1]));
  LDR(W14, GetContextReg(), offsetof(ppc::PPCContext, lr));
  STR(W13, X10, offsetof(A64BackendStackpoint, guest_sp));
  STR(W14, X10, offsetof(A64BackendStackpoint, guest_return_address));
  MOV(W15, static_cast<uint32_t>(stack_size()));
  STR(W15, X10, offsetof(A64BackendStackpoint, stack_size));

  ADD(W11, W11, 1);
  STR(W11, X9, offsetof(A64BackendContext, current_stackpoint_depth));

  B(done);
  l(overflowed);
  CallNativeSafe(reinterpret_cast<void*>(HandleStackpointOverflowError));
  l(done);
}

void A64Emitter::PopStackpoint() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  oaknut::Label done;
  SUB(X9, GetContextReg(), sizeof(A64BackendContext));
  LDR(W10, X9, offsetof(A64BackendContext, current_stackpoint_depth));
  CBZ(W10, done);
  SUB(W10, W10, 1);
  STR(W10, X9, offsetof(A64BackendContext, current_stackpoint_depth));
  l(done);
}

void A64Emitter::EnsureSynchronizedGuestAndHostStack() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  auto* helper = backend()->stack_sync_helper();
  if (!helper) {
    return;
  }
  oaknut::Label done;
  // backend_ctx = context - sizeof(A64BackendContext)
  SUB(X9, GetContextReg(), sizeof(A64BackendContext));
  LDR(X10, X9, offsetof(A64BackendContext, stackpoints));
  CBZ(X10, done);
  LDR(W11, X9, offsetof(A64BackendContext, current_stackpoint_depth));
  CBZ(W11, done);

  // Compare guest SP against the top stackpoint.
  SUB(W11, W11, 1);
  LSL(X12, X11, 5);  // sizeof(A64BackendStackpoint) == 32
  ADD(X12, X10, X12);
  LDR(W13, GetContextReg(), offsetof(ppc::PPCContext, r[1]));
  LDR(W14, X12, offsetof(A64BackendStackpoint, guest_sp));
  CMP(W13, W14);
  B(oaknut::Cond::LS, done);

  // Call helper to resynchronize host SP/FP to guest stack.
  MOV(X0, GetContextReg());
  MOV(X1, static_cast<uint64_t>(stack_size()));
  MOV(X16, reinterpret_cast<uint64_t>(helper));
  BLR(X16);

  l(done);
}

bool A64Emitter::ConstantFitsIn32Reg(uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    return true;
  } else if ((v & ~0x7FFFFFFFUL) == ~0x7FFFFFFFUL) {
    // Negative number that fits in 32bits.
    return true;
  }
  return false;
}

void A64Emitter::MovMem64(const oaknut::XRegSp& addr, intptr_t offset,
                          uint64_t v) {
  if (v == 0) {
    STR(XZR, addr, offset);
  } else if (!(v >> 32)) {
    // All high bits are zero, 32-bit MOV
    MOV(W0, static_cast<uint32_t>(v));
    STR(X0, addr, offset);
  } else {
    // 64bit number that needs double movs.
    MOV(X0, v);
    STR(X0, addr, offset);
  }
}

static const vec128_t v_consts[] = {
    /* VZero                */ vec128f(0.0f),
    /* VOnePD               */ vec128d(1.0),
    /* VNegativeOne         */ vec128f(-1.0f, -1.0f, -1.0f, -1.0f),
    /* VFFFF                */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VMaskX16Y16          */
    vec128i(0x0000FFFFu, 0xFFFF0000u, 0x00000000u, 0x00000000u),
    /* VFlipX16Y16          */
    vec128i(0x00008000u, 0x00000000u, 0x00000000u, 0x00000000u),
    /* VFixX16Y16           */ vec128f(-32768.0f, 0.0f, 0.0f, 0.0f),
    /* VNormalizeX16Y16     */
    vec128f(1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f),
    /* V0001                */ vec128f(0.0f, 0.0f, 0.0f, 1.0f),
    /* V3301                */ vec128f(3.0f, 3.0f, 0.0f, 1.0f),
    /* V3331                */ vec128f(3.0f, 3.0f, 3.0f, 1.0f),
    /* V3333                */ vec128f(3.0f, 3.0f, 3.0f, 3.0f),
    /* VSignMaskPS          */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* VSignMaskPD          */
    vec128i(0x00000000u, 0x80000000u, 0x00000000u, 0x80000000u),
    /* VAbsMaskPS           */
    vec128i(0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu),
    /* VAbsMaskPD           */
    vec128i(0xFFFFFFFFu, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFFu),
    /* VByteSwapMask        */
    vec128i(0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu),
    /* VByteOrderMask       */
    vec128i(0x01000302u, 0x05040706u, 0x09080B0Au, 0x0D0C0F0Eu),
    /* VPermuteControl15    */ vec128b(15),
    /* VPermuteByteMask     */ vec128b(0x1F),
    /* VPackD3DCOLORSat     */ vec128i(0x404000FFu),
    /* VPackD3DCOLOR        */
    // Note: x86 PSHUFB uses 0xFF to zero bytes, ARM TBL uses indices >= 16
    // Keep original 0xFF for consistency, handle in implementation
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x0C000408u),
    /* VUnpackD3DCOLOR      */
    vec128i(0xFFFFFF0Eu, 0xFFFFFF0Du, 0xFFFFFF0Cu, 0xFFFFFF0Fu),
    /* VPackFLOAT16_2       */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000302u),
    /* VUnpackFLOAT16_2     */
    vec128i(0x0D0C0F0Eu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VPackFLOAT16_4       */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000302u, 0x05040706u),
    /* VUnpackFLOAT16_4     */
    vec128i(0x09080B0Au, 0x0D0C0F0Eu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VPackSHORT_Min       */ vec128i(0x403F8001u),
    /* VPackSHORT_Max       */ vec128i(0x40407FFFu),
    /* VPackSHORT_2         */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000504u),
    /* VPackSHORT_4         */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000504u, 0x09080D0Cu),
    /* VUnpackSHORT_2       */
    vec128i(0xFFFF0F0Eu, 0xFFFF0D0Cu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VUnpackSHORT_4       */
    vec128i(0xFFFF0B0Au, 0xFFFF0908u, 0xFFFF0F0Eu, 0xFFFF0D0Cu),
    /* VUnpackSHORT_Overflow */ vec128i(0x403F8000u),
    /* VPackUINT_2101010_MinUnpacked */
    vec128i(0x403FFE01u, 0x403FFE01u, 0x403FFE01u, 0x40400000u),
    /* VPackUINT_2101010_MaxUnpacked */
    vec128i(0x404001FFu, 0x404001FFu, 0x404001FFu, 0x40400003u),
    /* VPackUINT_2101010_MaskUnpacked */
    vec128i(0x3FFu, 0x3FFu, 0x3FFu, 0x3u),
    /* VPackUINT_2101010_MaskPacked */
    vec128i(0x3FFu, 0x3FFu << 10, 0x3FFu << 20, 0x3u << 30),
    /* VPackUINT_2101010_Shift */ vec128i(0, 10, 20, 30),
    /* VUnpackUINT_2101010_Overflow */ vec128i(0x403FFE00u),
    /* VPackULONG_4202020_MinUnpacked */
    vec128i(0x40380001u, 0x40380001u, 0x40380001u, 0x40400000u),
    /* VPackULONG_4202020_MaxUnpacked */
    vec128i(0x4047FFFFu, 0x4047FFFFu, 0x4047FFFFu, 0x4040000Fu),
    /* VPackULONG_4202020_MaskUnpacked */
    vec128i(0xFFFFFu, 0xFFFFFu, 0xFFFFFu, 0xFu),
    /* VPackULONG_4202020_PermuteXZ */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x0A0908FFu, 0xFF020100u),
    /* VPackULONG_4202020_PermuteYW */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x0CFFFF06u, 0x0504FFFFu),
    /* VUnpackULONG_4202020_Permute */
    vec128i(0xFF0E0D0Cu, 0xFF0B0A09u, 0xFF080F0Eu, 0xFFFFFF0Bu),
    /* VUnpackULONG_4202020_Overflow */ vec128i(0x40380000u),
    /* VOneOver255          */ vec128f(1.0f / 255.0f),
    /* VMaskEvenPI16        */
    vec128i(0x0000FFFFu, 0x0000FFFFu, 0x0000FFFFu, 0x0000FFFFu),
    /* VShiftMaskEvenPI16   */
    vec128i(0x0000000Fu, 0x0000000Fu, 0x0000000Fu, 0x0000000Fu),
    /* VShiftMaskPS         */
    vec128i(0x0000001Fu, 0x0000001Fu, 0x0000001Fu, 0x0000001Fu),
    /* VShiftByteMask       */
    vec128i(0x000000FFu, 0x000000FFu, 0x000000FFu, 0x000000FFu),
    /* VSwapWordMask        */
    vec128i(0x03030303u, 0x03030303u, 0x03030303u, 0x03030303u),
    /* VUnsignedDwordMax    */
    vec128i(0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu, 0x00000000u),
    /* V255                 */ vec128f(255.0f),
    /* VPI32                */ vec128i(32),
    /* VSignMaskI8          */
    vec128i(0x80808080u, 0x80808080u, 0x80808080u, 0x80808080u),
    /* VSignMaskI16         */
    vec128i(0x80008000u, 0x80008000u, 0x80008000u, 0x80008000u),
    /* VSignMaskI32         */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* VSignMaskF32         */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* VShortMinPS          */ vec128f(SHRT_MIN),
    /* VShortMaxPS          */ vec128f(SHRT_MAX),
    /* VIntMin              */ vec128i(INT_MIN),
    /* VIntMax              */ vec128i(INT_MAX),
    /* VIntMaxPD            */ vec128d(INT_MAX),
    /* VPosIntMinPS         */ vec128f((float)0x80000000u),
    /* VQNaN                */ vec128i(0x7FC00000u),
    /* VInt127              */ vec128i(0x7Fu),
    /* V2To32               */ vec128f(0x1.0p32f),
    /* VSingleDenormalMask  */ vec128i(0x7F800000u),
};

// First location to try and place constants.
[[maybe_unused]] static const uintptr_t kConstDataLocation = 0x20000000;
static const uintptr_t kConstDataSize = sizeof(v_consts);

// Increment the location by this amount for every allocation failure.
[[maybe_unused]] static const uintptr_t kConstDataIncrement = 0x00001000;

// This function places constant data that is used by the emitter later on.
// Only called once and used by multiple instances of the emitter.
//
// TODO(DrChat): This should be placed in the code cache with the code, but
// doing so requires RIP-relative addressing, which is difficult to support
// given the current setup.
uintptr_t A64Emitter::PlaceConstData() {
  void* mem = nullptr;
#if XE_PLATFORM_APPLE && XE_ARCH_ARM64
  // macOS ARM64 PAGEZERO blocks low fixed mappings; use OS-chosen addresses.
  mem = memory::AllocFixed(
      nullptr, xe::round_up(kConstDataSize, memory::page_size()),
      memory::AllocationType::kReserveCommit, memory::PageAccess::kReadWrite);
#else
  uint8_t* ptr = reinterpret_cast<uint8_t*>(kConstDataLocation);
  while (!mem) {
    mem = memory::AllocFixed(
        ptr, xe::round_up(kConstDataSize, memory::page_size()),
        memory::AllocationType::kReserveCommit, memory::PageAccess::kReadWrite);

    ptr += kConstDataIncrement;
  }
#endif

#if XE_PLATFORM_APPLE && XE_ARCH_ARM64
  // On macOS ARM64, memory is often allocated in high address space
  if (reinterpret_cast<uintptr_t>(mem) & ~0x7FFFFFFF) {
    XELOGD(
        "Const data allocated at high address {:#x}, may cause compatibility "
        "issues",
        reinterpret_cast<uintptr_t>(mem));
    // Continue anyway since we'll handle it later
  }
#else
  // The pointer must not be greater than 31 bits.
  assert_zero(reinterpret_cast<uintptr_t>(mem) & ~0x7FFFFFFF);
#endif
  std::memcpy(mem, v_consts, sizeof(v_consts));
  memory::Protect(mem, kConstDataSize, memory::PageAccess::kReadOnly, nullptr);

  return reinterpret_cast<uintptr_t>(mem);
}

void A64Emitter::FreeConstData(uintptr_t data) {
  memory::DeallocFixed(reinterpret_cast<void*>(data), 0,
                       memory::DeallocationType::kRelease);
}

uintptr_t A64Emitter::GetVConstPtr() const { return backend_->emitter_data(); }

uintptr_t A64Emitter::GetVConstPtr(VConst id) const {
  // Load through fixed constant table setup by PlaceConstData.
  // It's important that the pointer is not signed, as it will be sign-extended.
  return GetVConstPtr() + GetVConstOffset(id);
}

// Attempts to convert an fp32 bit-value into an fp8-immediate value for FMOV
// returns false if the value cannot be represented
// C2.2.3 Modified immediate constants in A64 floating-point instructions
// abcdefgh
//    V
// aBbbbbbc defgh000 00000000 00000000
// B = NOT(b)
static bool f32_to_fimm8(uint32_t u32, oaknut::FImm8& fp8) {
  const uint32_t sign = (u32 >> 31) & 1;
  int32_t exp = ((u32 >> 23) & 0xff) - 127;
  int64_t mantissa = u32 & 0x7fffff;

  // Too many mantissa bits
  if (mantissa & 0x7ffff) {
    return false;
  }
  fpcr_mode_ = new_mode;
  if (!already_set) {
    // Load the pre-computed FPCR value from the backend context.
    // This avoids an expensive MRS + read-modify-write cycle.
    auto bctx = GetBackendCtxReg();
    if (new_mode == FPCRMode::Vmx) {
      ldr(w0, Xbyak_aarch64::ptr(bctx, static_cast<uint32_t>(offsetof(
                                           A64BackendContext, fpcr_vmx))));
    } else {
      ldr(w0, Xbyak_aarch64::ptr(bctx, static_cast<uint32_t>(offsetof(
                                           A64BackendContext, fpcr_fpu))));
    }
    msr(3, 3, 4, 4, 0, x0);  // msr FPCR, x0
  }
  return true;
}

Label& A64Emitter::AddToTail(TailEmitCallback callback, uint32_t alignment) {
  TailEmitter tail;
  tail.alignment = alignment;
  tail.func = std::move(callback);
  tail_code_.push_back(std::move(tail));
  return tail_code_.back().label;
}

Label& A64Emitter::NewCachedLabel() {
  auto* label = new Label();
  label_cache_.push_back(label);
  return *label;
}

Label& A64Emitter::GetLabel(uint32_t label_id) {
  auto it = label_map_.find(label_id);
  if (it != label_map_.end()) {
    return *it->second;
  }
  auto* label = new Label();
  label_map_[label_id] = label;
  return *label;
}

void A64Emitter::HandleStackpointOverflowError(ppc::PPCContext* context) {
  if (debugging::IsDebuggerAttached()) {
    debugging::Break();
  }
  xe::FatalError(
      "Overflowed stackpoints! Please report this error for this title to "
      "Xenia developers.");
}

void A64Emitter::PushStackpoint() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // x8 = stackpoints array, w9 = current depth
  ldr(x8, ptr(x19,
              static_cast<uint32_t>(offsetof(A64BackendContext, stackpoints))));
  ldr(w9, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));

  // Compute offset into array: x10 = w9 * sizeof(A64BackendStackpoint)
  mov(w10, static_cast<uint32_t>(sizeof(A64BackendStackpoint)));
  umull(x10, w9, w10);
  add(x8, x8, x10);

  // Store host SP.
  mov(x10, sp);
  str(x10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, host_stack_))));
  // Store guest r1 (32-bit).
  ldr(w10, ptr(x20, static_cast<int32_t>(offsetof(ppc::PPCContext, r[1]))));
  str(w10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, guest_stack_))));
  // Store guest LR (32-bit).
  ldr(w10, ptr(x20, static_cast<int32_t>(offsetof(ppc::PPCContext, lr))));
  str(w10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, guest_return_address_))));

  // Increment depth.
  add(w9, w9, 1);
  str(w9, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));

  // Check for overflow.
  mov(w10, static_cast<uint32_t>(cvars::a64_max_stackpoints));
  cmp(w9, w10);
  auto& overflow_label = AddToTail([](A64Emitter& e, Label& lbl) {
    e.CallNativeSafe(
        reinterpret_cast<void*>(A64Emitter::HandleStackpointOverflowError));
  });
  b(GE, overflow_label);
}

void A64Emitter::PopStackpoint() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // Decrement current_stackpoint_depth.
  ldr(w8, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));
  sub(w8, w8, 1);
  str(w8, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));
}

void A64Emitter::EnsureSynchronizedGuestAndHostStack() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // Compare current stackpoint depth against the value saved after
  // PushStackpoint in the prolog. If different, a longjmp occurred and
  // some frames' PopStackpoint never ran.
  auto& return_from_sync = NewCachedLabel();

  ldr(w17, ptr(x19, static_cast<uint32_t>(offsetof(A64BackendContext,
                                                   current_stackpoint_depth))));
  ldr(w16, ptr(sp, static_cast<uint32_t>(
                       StackLayout::GUEST_SAVED_STACKPOINT_DEPTH)));
  cmp(w17, w16);

  auto& sync_label = AddToTail([&return_from_sync](A64Emitter& e, Label& lbl) {
    // Set up arguments for the sync helper:
    //   x8 = return address (where to resume after fixup)
    //   x9 = this function's stack size
    e.adr(e.x8, return_from_sync);
    e.mov(e.x9, static_cast<uint64_t>(e.stack_size()));
    e.mov(e.x10, reinterpret_cast<uint64_t>(
                     e.backend()->synchronize_guest_and_host_stack_helper()));
    e.br(e.x10);
  });
  b(NE, sync_label);

  L(return_from_sync);
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

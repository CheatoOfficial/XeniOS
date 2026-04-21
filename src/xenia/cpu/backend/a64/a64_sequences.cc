/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"
#include "xenia/cpu/backend/a64/a64_op.h"
#include "xenia/cpu/backend/a64/a64_seq_util.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/backend/a64/a64_tracers.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/ppc/ppc_context.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace xe::cpu::hir;
using namespace Xbyak_aarch64;

std::unordered_map<uint32_t, SequenceSelectFn>& SequenceTable() {
  static auto* sequence_table =
      new std::unordered_map<uint32_t, SequenceSelectFn>();
  return *sequence_table;
}

// ============================================================================
// Debug validation helpers
// ============================================================================
// Validates that a binary op with constant src1 won't clobber src2
// when dest and src2 share the same physical register.
// Call this at JIT-compile time (not in emitted code).
template <typename DEST, typename SRC2>
static void AssertNoClobber(const DEST& dest, const SRC2& src2) {
  // If src2 is a register (not constant) and dest is the same register,
  // the caller must use a scratch register for the constant.
  if (!src2.is_constant) {
    assert_true(dest.reg().getIdx() != src2.reg().getIdx() &&
                "Binary op with constant src1: dest == src2 would clobber! "
                "Use a scratch register for the constant.");
  }
}

// ============================================================================
// Safe binary operation helpers
// ============================================================================
// Emits dest = op(src1_const, src2_reg) safely, using a scratch register
// to avoid clobbering src2 when dest and src2 are the same register.
// Usage: EmitSafeBinaryConst1(e, i.dest, imm, i.src2, op_fn)
template <typename REG, typename FN>
static void EmitBinaryConstLhs(A64Emitter& e, const REG& dest,
                               uint64_t src1_const, const REG& src2,
                               const FN& op_fn) {
  // Always use scratch to avoid clobbering src2 if dest == src2.
  if constexpr (std::is_same_v<REG, WReg>) {
    e.mov(e.w17, src1_const);
    op_fn(e, dest, WReg(17), src2);
  } else {
    e.mov(e.x17, src1_const);
    op_fn(e, dest, XReg(17), src2);
  }
}

template <typename Fn>
void EmitWithVmxFpcr(A64Emitter& e, Fn&& emit_op) {
  // Altivec vector FP runs with non-IEEE flush-to-zero and round-to-nearest.
  e.MRS(X13, SystemReg::FPCR);
  e.MOV(X14, X13);
  e.BFI(X14, XZR, 22, 2);
  e.ORR(X14, X14, uint64_t(1) << 24);
  e.MSR(SystemReg::FPCR, X14);
  emit_op();
  e.MSR(SystemReg::FPCR, X13);
}

inline bool IsMulAddOrMulSubDef(const Value* value) {
  if (!value || !value->def || !value->def->opcode) {
    return false;
  }
  const auto opcode = value->def->opcode->num;
  return opcode == OPCODE_MUL_ADD || opcode == OPCODE_MUL_SUB;
}

inline void EmitPropagateInputNanF64Binary(A64Emitter& e, DReg dest, DReg src1,
                                           DReg src2, oaknut::Label& no_nan,
                                           oaknut::Label& done) {
  constexpr uint64_t kF64QuietBit = uint64_t(1) << 51;
  oaknut::Label check_src2;
  oaknut::Label src1_qnan;
  oaknut::Label src2_qnan;

  e.FCMP(src1, src1);
  e.B(Cond::VC, check_src2);
  e.FMOV(X0, src1);
  e.TST(X0, kF64QuietBit);
  e.B(Cond::NE, src1_qnan);
  e.ORR(X0, X0, kF64QuietBit);
  e.FMOV(dest, X0);
  e.B(done);
  e.l(src1_qnan);
  e.FMOV(dest, src1);
  e.B(done);

  e.l(check_src2);
  e.FCMP(src2, src2);
  e.B(Cond::VC, no_nan);
  e.FMOV(X0, src2);
  e.TST(X0, kF64QuietBit);
  e.B(Cond::NE, src2_qnan);
  e.ORR(X0, X0, kF64QuietBit);
  e.FMOV(dest, X0);
  e.B(done);
  e.l(src2_qnan);
  e.FMOV(dest, src2);
  e.B(done);
}

inline void EmitPropagateInputNanF64Ternary(A64Emitter& e, DReg dest, DReg src1,
                                            DReg src2, DReg src3,
                                            oaknut::Label& no_nan,
                                            oaknut::Label& done) {
  constexpr uint64_t kF64QuietBit = uint64_t(1) << 51;

  oaknut::Label check_src2;
  oaknut::Label check_src3;
  oaknut::Label src1_qnan;
  oaknut::Label src2_qnan;
  oaknut::Label src3_qnan;

  e.FCMP(src1, src1);
  e.B(Cond::VC, check_src2);
  e.FMOV(X0, src1);
  e.TST(X0, kF64QuietBit);
  e.B(Cond::NE, src1_qnan);
  e.ORR(X0, X0, kF64QuietBit);
  e.FMOV(dest, X0);
  e.B(done);
  e.l(src1_qnan);
  e.FMOV(dest, src1);
  e.B(done);

  e.l(check_src2);
  e.FCMP(src2, src2);
  e.B(Cond::VC, check_src3);
  e.FMOV(X0, src2);
  e.TST(X0, kF64QuietBit);
  e.B(Cond::NE, src2_qnan);
  e.ORR(X0, X0, kF64QuietBit);
  e.FMOV(dest, X0);
  e.B(done);
  e.l(src2_qnan);
  e.FMOV(dest, src2);
  e.B(done);

  e.l(check_src3);
  e.FCMP(src3, src3);
  e.B(Cond::VC, no_nan);
  e.FMOV(X0, src3);
  e.TST(X0, kF64QuietBit);
  e.B(Cond::NE, src3_qnan);
  e.ORR(X0, X0, kF64QuietBit);
  e.FMOV(dest, X0);
  e.B(done);
  e.l(src3_qnan);
  e.FMOV(dest, src3);
  e.B(done);
}

inline void EmitSelectFirstNanLanesF32x4(A64Emitter& e, QReg result, QReg src1,
                                         QReg src2) {
  // mask1 = isnan(src1)
  e.FCMEQ(Q3.S4(), src1.S4(), src1.S4());
  e.MVN(Q3.B16(), Q3.B16());
  e.BIT(result.B16(), src1.B16(), Q3.B16());

  // mask2 = !mask1 & isnan(src2)
  e.FCMEQ(Q0.S4(), src2.S4(), src2.S4());
  e.MVN(Q0.B16(), Q0.B16());
  e.BIC(Q0.B16(), Q0.B16(), Q3.B16());
  e.BIT(result.B16(), src2.B16(), Q0.B16());
}

inline void EmitSelectFirstQNanLanesF32x4(A64Emitter& e, QReg result, QReg src1,
                                          QReg src2, QReg src3) {
  // Prefer first NaN source lane and quiet signaling NaNs.
  e.LoadConstantV(Q6, vec128i(0x00400000u));

  e.FCMEQ(Q7.S4(), src1.S4(), src1.S4());
  e.MVN(Q7.B16(), Q7.B16());
  e.ORR(Q4.B16(), src1.B16(), Q6.B16());
  e.BIT(result.B16(), Q4.B16(), Q7.B16());

  e.FCMEQ(Q0.S4(), src2.S4(), src2.S4());
  e.MVN(Q0.B16(), Q0.B16());
  e.BIC(Q0.B16(), Q0.B16(), Q7.B16());
  e.ORR(Q4.B16(), src2.B16(), Q6.B16());
  e.BIT(result.B16(), Q4.B16(), Q0.B16());

  e.FCMEQ(Q3.S4(), src3.S4(), src3.S4());
  e.MVN(Q3.B16(), Q3.B16());
  e.ORR(Q4.B16(), src3.B16(), Q6.B16());
  e.BIC(Q3.B16(), Q3.B16(), Q7.B16());
  e.BIC(Q3.B16(), Q3.B16(), Q0.B16());
  e.BIT(result.B16(), Q4.B16(), Q3.B16());
}

// ============================================================================
// OPCODE_COMMENT
// ============================================================================
struct COMMENT : Sequence<COMMENT, I<OPCODE_COMMENT, VoidOp, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (IsTracingInstr()) {
      auto str = reinterpret_cast<const char*>(i.src1.value);
      auto str_copy = strdup(str);
      e.mov(e.x1, reinterpret_cast<uint64_t>(str_copy));
      e.CallNative(reinterpret_cast<void*>(TraceString));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMMENT, COMMENT);

// ============================================================================
// OPCODE_NOP
// ============================================================================
struct NOP : Sequence<NOP, I<OPCODE_NOP, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { e.nop(); }
};
EMITTER_OPCODE_TABLE(OPCODE_NOP, NOP);

// ============================================================================
// OPCODE_SOURCE_OFFSET
// ============================================================================
struct SOURCE_OFFSET
    : Sequence<SOURCE_OFFSET, I<OPCODE_SOURCE_OFFSET, VoidOp, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.MarkSourceOffset(i.instr);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SOURCE_OFFSET, SOURCE_OFFSET);

// ============================================================================
// OPCODE_CONTEXT_BARRIER
// ============================================================================
struct CONTEXT_BARRIER
    : Sequence<CONTEXT_BARRIER, I<OPCODE_CONTEXT_BARRIER, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // No-op on ARM64 (context is always in x20).
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CONTEXT_BARRIER, CONTEXT_BARRIER);

// ============================================================================
// OPCODE_ASSIGN
// ============================================================================
struct ASSIGN_I8 : Sequence<ASSIGN_I8, I<OPCODE_ASSIGN, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    } else {
      e.mov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_I16 : Sequence<ASSIGN_I16, I<OPCODE_ASSIGN, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.mov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_I32 : Sequence<ASSIGN_I32, I<OPCODE_ASSIGN, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    } else {
      e.mov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_I64 : Sequence<ASSIGN_I64, I<OPCODE_ASSIGN, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
    } else {
      e.mov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_F32 : Sequence<ASSIGN_F32, I<OPCODE_ASSIGN, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // Load constant float via GPR.
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(i.dest, e.w0);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_F64 : Sequence<ASSIGN_F64, I<OPCODE_ASSIGN, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(i.dest, e.x0);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_V128 : Sequence<ASSIGN_V128, I<OPCODE_ASSIGN, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      LoadV128Const(e, i.dest.reg().getIdx(), i.src1.constant());
    } else {
      // mov vD.16b, vS.16b (via ORR trick: orr vD.16b, vS.16b, vS.16b)
      auto src_vreg = VReg(i.src1.reg().getIdx());
      auto dst_vreg = VReg(i.dest.reg().getIdx());
      e.orr(dst_vreg.b16, src_vreg.b16, src_vreg.b16);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ASSIGN, ASSIGN_I8, ASSIGN_I16, ASSIGN_I32,
                     ASSIGN_I64, ASSIGN_F32, ASSIGN_F64, ASSIGN_V128);

// ============================================================================
// OPCODE_LOAD_CONTEXT
// ============================================================================
struct LOAD_CONTEXT_I8
    : Sequence<LOAD_CONTEXT_I8, I<OPCODE_LOAD_CONTEXT, I8Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // ldrb wD, [x20, #offset]
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldrb(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_I16
    : Sequence<LOAD_CONTEXT_I16, I<OPCODE_LOAD_CONTEXT, I16Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldrh(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_I32
    : Sequence<LOAD_CONTEXT_I32, I<OPCODE_LOAD_CONTEXT, I32Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_I64
    : Sequence<LOAD_CONTEXT_I64, I<OPCODE_LOAD_CONTEXT, I64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_F32
    : Sequence<LOAD_CONTEXT_F32, I<OPCODE_LOAD_CONTEXT, F32Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_F64
    : Sequence<LOAD_CONTEXT_F64, I<OPCODE_LOAD_CONTEXT, F64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_V128
    : Sequence<LOAD_CONTEXT_V128, I<OPCODE_LOAD_CONTEXT, V128Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_CONTEXT, LOAD_CONTEXT_I8, LOAD_CONTEXT_I16,
                     LOAD_CONTEXT_I32, LOAD_CONTEXT_I64, LOAD_CONTEXT_F32,
                     LOAD_CONTEXT_F64, LOAD_CONTEXT_V128);

// ============================================================================
// OPCODE_STORE_CONTEXT
// ============================================================================
struct STORE_CONTEXT_I8
    : Sequence<STORE_CONTEXT_I8,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (i.src2.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFF));
      e.strb(e.w0, ptr(e.GetContextReg(), offset));
    } else {
      e.strb(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_I16
    : Sequence<STORE_CONTEXT_I16,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (i.src2.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
      e.strh(e.w0, ptr(e.GetContextReg(), offset));
    } else {
      e.strh(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_I32
    : Sequence<STORE_CONTEXT_I32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0) {
        e.str(e.wzr, ptr(e.GetContextReg(), offset));
      } else {
        e.mov(e.w0,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
        e.str(e.w0, ptr(e.GetContextReg(), offset));
      }
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_I64
    : Sequence<STORE_CONTEXT_I64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0) {
        e.str(e.xzr, ptr(e.GetContextReg(), offset));
      } else {
        e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
        e.str(e.x0, ptr(e.GetContextReg(), offset));
      }
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_F32
    : Sequence<STORE_CONTEXT_F32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.str(e.w0, ptr(e.GetContextReg(), offset));
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_F64
    : Sequence<STORE_CONTEXT_F64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.str(e.x0, ptr(e.GetContextReg(), offset));
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_V128
    : Sequence<STORE_CONTEXT_V128,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (i.src2.is_constant) {
      LoadV128Const(e, 0, i.src2.constant());
      e.str(QReg(0), ptr(e.GetContextReg(), offset));
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_CONTEXT, STORE_CONTEXT_I8, STORE_CONTEXT_I16,
                     STORE_CONTEXT_I32, STORE_CONTEXT_I64, STORE_CONTEXT_F32,
                     STORE_CONTEXT_F64, STORE_CONTEXT_V128);

// ============================================================================
// OPCODE_ADD (Integer)
// ============================================================================
struct ADD_I8 : Sequence<ADD_I8, I<OPCODE_ADD, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() + i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      e.add(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0xFF));
    } else if (i.src1.is_constant) {
      e.add(i.dest, i.src2, static_cast<uint32_t>(i.src1.constant() & 0xFF));
    } else {
      e.add(i.dest, i.src1, i.src2);
    }
  }
};
struct ADD_I16 : Sequence<ADD_I16, I<OPCODE_ADD, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() + i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      uint32_t imm = static_cast<uint32_t>(i.src2.constant() & 0xFFFF);
      if (imm <= 4095) {
        e.add(i.dest, i.src1, imm);
      } else {
        e.mov(e.w0, static_cast<uint64_t>(imm));
        e.add(i.dest, i.src1, e.w0);
      }
    } else if (i.src1.is_constant) {
      uint32_t imm = static_cast<uint32_t>(i.src1.constant() & 0xFFFF);
      if (imm <= 4095) {
        e.add(i.dest, i.src2, imm);
      } else {
        e.mov(e.w0, static_cast<uint64_t>(imm));
        e.add(i.dest, i.src2, e.w0);
      }
    } else {
      e.add(i.dest, i.src1, i.src2);
    }
  }
};
struct ADD_I32 : Sequence<ADD_I32, I<OPCODE_ADD, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() + i.src2.constant())));
    } else if (i.src2.is_constant) {
      uint32_t imm = static_cast<uint32_t>(i.src2.constant());
      if (imm <= 4095) {
        e.add(i.dest, i.src1, imm);
      } else {
        e.mov(e.w0, static_cast<uint64_t>(imm));
        e.add(i.dest, i.src1, e.w0);
      }
    } else if (i.src1.is_constant) {
      uint32_t imm = static_cast<uint32_t>(i.src1.constant());
      if (imm <= 4095) {
        e.add(i.dest, i.src2, imm);
      } else {
        e.mov(e.w0, static_cast<uint64_t>(imm));
        e.add(i.dest, i.src2, e.w0);
      }
    } else {
      e.add(i.dest, i.src1, i.src2);
    }
  }
};
struct ADD_I64 : Sequence<ADD_I64, I<OPCODE_ADD, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() + i.src2.constant()));
    } else if (i.src2.is_constant) {
      uint64_t imm = static_cast<uint64_t>(i.src2.constant());
      if (imm <= 4095) {
        e.add(i.dest, i.src1, static_cast<uint32_t>(imm));
      } else {
        e.mov(e.x0, imm);
        e.add(i.dest, i.src1, e.x0);
      }
    } else if (i.src1.is_constant) {
      uint64_t imm = static_cast<uint64_t>(i.src1.constant());
      if (imm <= 4095) {
        e.add(i.dest, i.src2, static_cast<uint32_t>(imm));
      } else {
        e.mov(e.x0, imm);
        e.add(i.dest, i.src2, e.x0);
      }
    } else {
      e.add(i.dest, i.src1, i.src2);
    }
  }
};
// NaN canonicalization helpers.
// PPC NaN selection for 2-operand FP ops (add, sub, mul, div):
// First NaN by operand position wins, quieted if SNaN.
// If no input is NaN, use hardware; generated NaN becomes PPC default QNaN.
// ARM64 may propagate a different NaN than PPC's positional rule, so NaN
// inputs are handled entirely in software.
enum class FpBinOp { Add, Sub, Mul, Div };

static void EmitFpBinOpWithPpcNan_F32(A64Emitter& e, SReg dest, SReg s1,
                                      SReg s2, FpBinOp op) {
  // Ensure FPU FPCR (no flush-to-zero) for scalar operations.
  e.ChangeFpcrMode(FPCRMode::Fpu);
  auto& nan_path = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();

  // Check if either input is NaN. fccmp sets NZCV from immediate if the
  // condition is false (i.e. s1 was already NaN), preserving V=1.
  e.fcmp(s1, s1);
  e.fccmp(s2, s2, 0b0001, VC);
  e.b(VS, nan_path);

  // Fast path: no NaN input — hardware op.
  switch (op) {
    case FpBinOp::Add:
      e.fadd(dest, s1, s2);
      break;
    case FpBinOp::Sub:
      e.fsub(dest, s1, s2);
      break;
    case FpBinOp::Mul:
      e.fmul(dest, s1, s2);
      break;
    case FpBinOp::Div:
      e.fdiv(dest, s1, s2);
      break;
  }
  e.fcmp(dest, dest);
  e.b(VC, done);
  e.mov(e.w0, static_cast<uint64_t>(0xFFC00000u));
  e.fmov(dest, e.w0);
  e.b(done);

  // Slow path: first NaN by position wins, quiet if SNaN.
  e.L(nan_path);
  auto& s1_not_nan = e.NewCachedLabel();
  e.fcmp(s1, s1);
  e.b(VC, s1_not_nan);
  e.fmov(e.w0, s1);
  e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
  e.fmov(dest, e.w0);
  e.b(done);
  e.L(s1_not_nan);
  e.fmov(e.w0, s2);
  e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
  e.fmov(dest, e.w0);

  e.L(done);
}

static void EmitFpBinOpWithPpcNan_F64(A64Emitter& e, DReg dest, DReg s1,
                                      DReg s2, FpBinOp op) {
  e.ChangeFpcrMode(FPCRMode::Fpu);
  auto& nan_path = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();

  // Check if either input is NaN. fccmp sets NZCV from immediate if the
  // condition is false (i.e. s1 was already NaN), preserving V=1.
  e.fcmp(s1, s1);
  e.fccmp(s2, s2, 0b0001, VC);
  e.b(VS, nan_path);

  // Fast path: no NaN input — hardware op.
  switch (op) {
    case FpBinOp::Add:
      e.fadd(dest, s1, s2);
      break;
    case FpBinOp::Sub:
      e.fsub(dest, s1, s2);
      break;
    case FpBinOp::Mul:
      e.fmul(dest, s1, s2);
      break;
    case FpBinOp::Div:
      e.fdiv(dest, s1, s2);
      break;
  }
  e.fcmp(dest, dest);
  e.b(VC, done);
  e.mov(e.x0, static_cast<uint64_t>(0xFFF8000000000000ull));
  e.fmov(dest, e.x0);
  e.b(done);

  // Slow path: first NaN by position wins, quiet if SNaN.
  e.L(nan_path);
  auto& s1_not_nan = e.NewCachedLabel();
  e.fcmp(s1, s1);
  e.b(VC, s1_not_nan);
  e.fmov(e.x0, s1);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));
  e.fmov(dest, e.x0);
  e.b(done);
  e.L(s1_not_nan);
  e.fmov(e.x0, s2);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));
  e.fmov(dest, e.x0);

  e.L(done);
}
// PPC FMA NaN selection (PowerISA 4.6.7.2):
// The first NaN operand by position (frA=s1, frC=s2, frB=s3) wins,
// regardless of QNaN vs SNaN.  If it's an SNaN, quiet it (set the
// quiet bit).  If no operand is NaN, use hardware FMA; generated NaN
// (from 0*inf or inf-inf) becomes the PPC default QNaN.
// ARM64's fmadd may propagate a different NaN than PPC's positional
// rule, so NaN inputs are handled entirely in software.
static void EmitFmaWithPpcNan_F64(A64Emitter& e, DReg dest, DReg s1, DReg s2,
                                  DReg s3, bool is_sub) {
  e.ChangeFpcrMode(FPCRMode::Fpu);
  auto& nan_path = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();

  // Quick check: any NaN among the three operands?
  e.fcmp(s1, s1);
  e.fccmp(s2, s2, 0b0001, VC);
  e.fccmp(s3, s3, 0b0001, VC);
  e.b(VS, nan_path);

  // Fast path: no NaN input → hardware FMA.
  if (is_sub)
    e.fnmsub(dest, s1, s2, s3);
  else
    e.fmadd(dest, s1, s2, s3);
  // If result is NaN (0*inf or inf-inf), canonicalize to PPC default.
  e.fcmp(dest, dest);
  e.b(VC, done);
  e.mov(e.x0, static_cast<uint64_t>(0xFFF8000000000000ull));
  e.fmov(dest, e.x0);
  e.b(done);

  // Slow path: first NaN by position wins (quiet if SNaN).
  e.L(nan_path);
  auto& s1_not_nan = e.NewCachedLabel();
  e.fcmp(s1, s1);
  e.b(VC, s1_not_nan);
  e.fmov(e.x0, s1);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));  // ensure quiet
  e.fmov(dest, e.x0);
  e.b(done);
  e.L(s1_not_nan);

  auto& s2_not_nan = e.NewCachedLabel();
  e.fcmp(s2, s2);
  e.b(VC, s2_not_nan);
  e.fmov(e.x0, s2);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));
  e.fmov(dest, e.x0);
  e.b(done);
  e.L(s2_not_nan);

  // Must be s3 (at least one NaN exists).
  e.fmov(e.x0, s3);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));
  e.fmov(dest, e.x0);

  e.L(done);
}

static void EmitFmaWithPpcNan_F32(A64Emitter& e, SReg dest, SReg s1, SReg s2,
                                  SReg s3, bool is_sub) {
  e.ChangeFpcrMode(FPCRMode::Fpu);
  auto& nan_path = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();

  e.fcmp(s1, s1);
  e.fccmp(s2, s2, 0b0001, VC);
  e.fccmp(s3, s3, 0b0001, VC);
  e.b(VS, nan_path);

  if (is_sub)
    e.fnmsub(dest, s1, s2, s3);
  else
    e.fmadd(dest, s1, s2, s3);
  e.fcmp(dest, dest);
  e.b(VC, done);
  e.mov(e.w0, static_cast<uint64_t>(0xFFC00000u));
  e.fmov(dest, e.w0);
  e.b(done);

  e.L(nan_path);
  auto& s1_not_nan = e.NewCachedLabel();
  e.fcmp(s1, s1);
  e.b(VC, s1_not_nan);
  e.fmov(e.w0, s1);
  e.orr(e.w0, e.w0, static_cast<uint32_t>(1u << 22));
  e.fmov(dest, e.w0);
  e.b(done);
  e.L(s1_not_nan);

  auto& s2_not_nan = e.NewCachedLabel();
  e.fcmp(s2, s2);
  e.b(VC, s2_not_nan);
  e.fmov(e.w0, s2);
  e.orr(e.w0, e.w0, static_cast<uint32_t>(1u << 22));
  e.fmov(dest, e.w0);
  e.b(done);
  e.L(s2_not_nan);

  e.fmov(e.w0, s3);
  e.orr(e.w0, e.w0, static_cast<uint32_t>(1u << 22));
  e.fmov(dest, e.w0);

  e.L(done);
}

struct ADD_F32 : Sequence<ADD_F32, I<OPCODE_ADD, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    EmitFpBinOpWithPpcNan_F32(e, i.dest, s1, s2, FpBinOp::Add);
  }
};
struct ADD_F64 : Sequence<ADD_F64, I<OPCODE_ADD, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    EmitFpBinOpWithPpcNan_F64(e, i.dest, s1, s2, FpBinOp::Add);
  }
};
struct ADD_V128 : Sequence<ADD_V128, I<OPCODE_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVmxFpBinOp_V128(e, i.dest.reg().getIdx(), i.src1, i.src2,
                        VmxFpBinOp::Add);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD, ADD_I8, ADD_I16, ADD_I32, ADD_I64, ADD_F32,
                     ADD_F64, ADD_V128);

// ============================================================================
// OPCODE_ZERO_EXTEND
// ============================================================================
struct ZERO_EXTEND_I16_I8
    : Sequence<ZERO_EXTEND_I16_I8, I<OPCODE_ZERO_EXTEND, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // uxtb wD, wS (same as and wD, wS, #0xFF)
    e.uxtb(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I8
    : Sequence<ZERO_EXTEND_I32_I8, I<OPCODE_ZERO_EXTEND, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I8
    : Sequence<ZERO_EXTEND_I64_I8, I<OPCODE_ZERO_EXTEND, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Zero-extend 8-bit to 64-bit: AND with 0xFF in 32-bit clears upper 32.
    auto w_dest = WReg(i.dest.reg().getIdx());
    e.uxtb(w_dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I16
    : Sequence<ZERO_EXTEND_I32_I16, I<OPCODE_ZERO_EXTEND, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxth(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I16
    : Sequence<ZERO_EXTEND_I64_I16, I<OPCODE_ZERO_EXTEND, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto w_dest = WReg(i.dest.reg().getIdx());
    e.uxth(w_dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I32
    : Sequence<ZERO_EXTEND_I64_I32, I<OPCODE_ZERO_EXTEND, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // mov wD, wS implicitly zero-extends to 64 bits on ARM64.
    auto w_dest = WReg(i.dest.reg().getIdx());
    e.mov(w_dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ZERO_EXTEND, ZERO_EXTEND_I16_I8, ZERO_EXTEND_I32_I8,
                     ZERO_EXTEND_I64_I8, ZERO_EXTEND_I32_I16,
                     ZERO_EXTEND_I64_I16, ZERO_EXTEND_I64_I32);

// ============================================================================
// OPCODE_SIGN_EXTEND
// ============================================================================
struct SIGN_EXTEND_I16_I8
    : Sequence<SIGN_EXTEND_I16_I8, I<OPCODE_SIGN_EXTEND, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I8
    : Sequence<SIGN_EXTEND_I32_I8, I<OPCODE_SIGN_EXTEND, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I8
    : Sequence<SIGN_EXTEND_I64_I8, I<OPCODE_SIGN_EXTEND, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I16
    : Sequence<SIGN_EXTEND_I32_I16, I<OPCODE_SIGN_EXTEND, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxth(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I16
    : Sequence<SIGN_EXTEND_I64_I16, I<OPCODE_SIGN_EXTEND, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxth(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I32
    : Sequence<SIGN_EXTEND_I64_I32, I<OPCODE_SIGN_EXTEND, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtw(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SIGN_EXTEND, SIGN_EXTEND_I16_I8, SIGN_EXTEND_I32_I8,
                     SIGN_EXTEND_I64_I8, SIGN_EXTEND_I32_I16,
                     SIGN_EXTEND_I64_I16, SIGN_EXTEND_I64_I32);

// ============================================================================
// OPCODE_TRUNCATE
// ============================================================================
struct TRUNCATE_I8_I16
    : Sequence<TRUNCATE_I8_I16, I<OPCODE_TRUNCATE, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Keep only low 8 bits.
    e.uxtb(i.dest, i.src1);
  }
};
struct TRUNCATE_I8_I32
    : Sequence<TRUNCATE_I8_I32, I<OPCODE_TRUNCATE, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, i.src1);
  }
};
struct TRUNCATE_I8_I64
    : Sequence<TRUNCATE_I8_I64, I<OPCODE_TRUNCATE, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto w_src = WReg(i.src1.reg().getIdx());
    e.uxtb(i.dest, w_src);
  }
};
struct TRUNCATE_I16_I32
    : Sequence<TRUNCATE_I16_I32, I<OPCODE_TRUNCATE, I16Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxth(i.dest, i.src1);
  }
};
struct TRUNCATE_I16_I64
    : Sequence<TRUNCATE_I16_I64, I<OPCODE_TRUNCATE, I16Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto w_src = WReg(i.src1.reg().getIdx());
    e.uxth(i.dest, w_src);
  }
};
struct TRUNCATE_I32_I64
    : Sequence<TRUNCATE_I32_I64, I<OPCODE_TRUNCATE, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // mov wD, wS — implicitly truncates (upper 32 bits zeroed).
    auto w_src = WReg(i.src1.reg().getIdx());
    e.mov(i.dest, w_src);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TRUNCATE, TRUNCATE_I8_I16, TRUNCATE_I8_I32,
                     TRUNCATE_I8_I64, TRUNCATE_I16_I32, TRUNCATE_I16_I64,
                     TRUNCATE_I32_I64);

// ============================================================================
// OPCODE_CONVERT
// ============================================================================
struct CONVERT_I32_F32
    : Sequence<CONVERT_I32_F32, I<OPCODE_CONVERT, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    const SReg src = i.src1.is_constant ? S0 : i.src1.reg().toS();
    if (i.src1.is_constant) {
      e.LoadConstantV(Q0, i.src1.constant());
    }
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.FCVTZS(i.dest, src);
    } else {
      e.FCVTNS(i.dest, src);
    }
  }
};
struct CONVERT_I32_F64
    : Sequence<CONVERT_I32_F64, I<OPCODE_CONVERT, I32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Intel returns 0x80000000 if the double value does not fit within an int32
    // ARM64 and PPC saturates the value instead
    const DReg src = i.src1.is_constant ? D0 : i.src1.reg().toD();
    if (i.src1.is_constant) {
      e.LoadConstantV(Q0, i.src1.constant());
    }
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.FCVTZS(i.dest, src);
    } else {
      e.FCVTNS(i.dest, src);
    }
  }
};
struct CONVERT_I64_F64
    : Sequence<CONVERT_I64_F64, I<OPCODE_CONVERT, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const DReg src = i.src1.is_constant ? D0 : i.src1.reg().toD();
    if (i.src1.is_constant) {
      e.LoadConstantV(Q0, i.src1.constant());
    }
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.FCVTZS(i.dest, src);
    } else {
      e.FCVTNS(i.dest, src);
    }
  }
};
struct CONVERT_F32_I32
    : Sequence<CONVERT_F32_I32, I<OPCODE_CONVERT, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.SCVTF(i.dest.reg().toS(), i.src1);
  }
};
struct CONVERT_F32_F64
    : Sequence<CONVERT_F32_F64, I<OPCODE_CONVERT, F32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const DReg src = i.src1.is_constant ? D0 : i.src1;
    if (i.src1.is_constant) {
      e.LoadConstantV(src.toQ(), i.src1.constant());
    }
    e.FCVT(i.dest.reg().toS(), src.toD());
  }
};
struct CONVERT_F64_I64
    : Sequence<CONVERT_F64_I64, I<OPCODE_CONVERT, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.SCVTF(i.dest.reg().toD(), i.src1);
  }
};
struct CONVERT_F64_F32
    : Sequence<CONVERT_F64_F32, I<OPCODE_CONVERT, F64Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const SReg src = i.src1.is_constant ? S0 : i.src1;
    if (i.src1.is_constant) {
      e.LoadConstantV(src.toQ(), i.src1.constant());
    }
    e.FCVT(i.dest.reg().toD(), src.toS());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CONVERT, CONVERT_I32_F32, CONVERT_I32_F64,
                     CONVERT_I64_F64, CONVERT_F32_I32, CONVERT_F32_F64,
                     CONVERT_F64_I64, CONVERT_F64_F32);

// ============================================================================
// OPCODE_TO_SINGLE
// ============================================================================
struct TOSINGLE_F64_F64
    : Sequence<TOSINGLE_F64_F64, I<OPCODE_TO_SINGLE, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const DReg src = i.src1.is_constant ? D1 : i.src1;
    if (i.src1.is_constant) {
      e.LoadConstantV(src.toQ(), i.src1.constant());
    }
    e.FCVT(S0, src);
    e.FCVT(i.dest, S0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TO_SINGLE, TOSINGLE_F64_F64);

// ============================================================================
// OPCODE_ROUND
// ============================================================================
struct ROUND_F32 : Sequence<ROUND_F32, I<OPCODE_ROUND, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const SReg src = i.src1.is_constant ? S0 : i.src1.reg().toS();
    if (i.src1.is_constant) {
      e.LoadConstantV(Q0, i.src1.constant());
    }
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.FRINTZ(i.dest.reg().toS(), src);
        break;
      case ROUND_TO_NEAREST:
        e.FRINTN(i.dest.reg().toS(), src);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.FRINTM(i.dest.reg().toS(), src);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.FRINTP(i.dest.reg().toS(), src);
        break;
    }
  }
};
struct ROUND_F64 : Sequence<ROUND_F64, I<OPCODE_ROUND, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const DReg src = i.src1.is_constant ? D0 : i.src1;
    if (i.src1.is_constant) {
      e.LoadConstantV(Q0, i.src1.constant());
    }
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.FRINTZ(i.dest, src);
        break;
      case ROUND_TO_NEAREST:
        e.FRINTN(i.dest, src);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.FRINTM(i.dest, src);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.FRINTP(i.dest, src);
        break;
    }
  }
};
struct ROUND_V128 : Sequence<ROUND_V128, I<OPCODE_ROUND, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    QReg src = i.src1.is_constant ? Q0 : i.src1.reg();
    if (i.src1.is_constant) {
      e.LoadConstantV(Q0, i.src1.constant());
    }
    EmitWithVmxFpcr(e, [&] {
      switch (i.instr->flags) {
        case ROUND_TO_ZERO:
          e.FRINTZ(i.dest.reg().S4(), src.S4());
          break;
        case ROUND_TO_NEAREST:
          e.FRINTN(i.dest.reg().S4(), src.S4());
          break;
        case ROUND_TO_MINUS_INFINITY:
          e.FRINTM(i.dest.reg().S4(), src.S4());
          break;
        case ROUND_TO_POSITIVE_INFINITY:
          e.FRINTP(i.dest.reg().S4(), src.S4());
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROUND, ROUND_F32, ROUND_F64, ROUND_V128);

// ============================================================================
// OPCODE_LOAD_CLOCK
// ============================================================================
struct LOAD_CLOCK : Sequence<LOAD_CLOCK, I<OPCODE_LOAD_CLOCK, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // When scaling is disabled and the raw clock source is selected, the code
    // in the Clock class is actually just forwarding tick counts after one
    // simple multiply and division. In that case we rather bake the scaling in
    // here to cut extra function calls with CPU cache misses and stack frame
    // overhead.
    if (cvars::clock_no_scaling && cvars::clock_source_raw) {
      auto ratio = Clock::guest_tick_ratio();
      // The 360 CPU is an in-order CPU, ARM64 usually isn't. Since it's
      // resolution however is much higher than the 360's mftb instruction this
      // can safely be ignored.

      // Read clock cycle count
      e.MRS(i.dest, SystemReg::CNTVCT_EL0);
      // Apply tick frequency scaling.
      e.MOV(X0, ratio.first);
      e.MUL(i.dest, i.dest, X0);
      e.MOV(X0, ratio.second);
      e.UDIV(i.dest, i.dest, X0);
    } else {
      e.CallNative(LoadClock);
      e.MOV(i.dest, X0);
    }
  }
  static uint64_t LoadClock(void* raw_context) {
    return Clock::QueryGuestTickCount();
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_CLOCK, LOAD_CLOCK);

// ============================================================================
// OPCODE_CONTEXT_BARRIER
// ============================================================================
struct CONTEXT_BARRIER
    : Sequence<CONTEXT_BARRIER, I<OPCODE_CONTEXT_BARRIER, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_CONTEXT_BARRIER, CONTEXT_BARRIER);

// ============================================================================
// OPCODE_MAX
// ============================================================================
struct MAX_F32 : Sequence<MAX_F32, I<OPCODE_MAX, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp<SReg>(
        e, i, [](A64Emitter& e, SReg dest, SReg src1, SReg src2) {
          e.FMAX(dest, src1, src2);
        });
  }
};
struct MAX_F64 : Sequence<MAX_F64, I<OPCODE_MAX, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp<DReg>(
        e, i, [](A64Emitter& e, DReg dest, DReg src1, DReg src2) {
          e.FMAX(dest, src1, src2);
        });
  }
};
struct MAX_V128 : Sequence<MAX_V128, I<OPCODE_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      QReg src1 = Q0;
      QReg src2 = Q1;
      if (i.src1.is_constant) {
        e.LoadConstantV(src1, i.src1.constant());
      } else {
        e.MOV(src1.B16(), i.src1.reg().B16());
      }
      if (i.src2.is_constant) {
        e.LoadConstantV(src2, i.src2.constant());
      } else {
        e.MOV(src2.B16(), i.src2.reg().B16());
      }

      e.FMAX(Q2.S4(), src1.S4(), src2.S4());
      EmitSelectFirstNanLanesF32x4(e, Q2, src1, src2);
      e.MOV(i.dest.reg().B16(), Q2.B16());
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MAX, MAX_F32, MAX_F64, MAX_V128);

// ============================================================================
// OPCODE_MIN
// ============================================================================
struct MIN_I8 : Sequence<MIN_I8, I<OPCODE_MIN, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](A64Emitter& e, WReg dest_src, WReg src) {
          e.CMP(dest_src, src);
          e.CSEL(dest_src, dest_src, src, Cond::LO);
        },
        [](A64Emitter& e, WReg dest_src, int32_t constant) {
          e.MOV(W0, constant);
          e.CMP(dest_src, W0);
          e.CSEL(dest_src, dest_src, W0, Cond::LO);
        });
  }
};
struct MIN_I16 : Sequence<MIN_I16, I<OPCODE_MIN, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](A64Emitter& e, WReg dest_src, WReg src) {
          e.CMP(dest_src, src);
          e.CSEL(dest_src, dest_src, src, Cond::LO);
        },
        [](A64Emitter& e, WReg dest_src, int32_t constant) {
          e.MOV(W0, constant);
          e.CMP(dest_src, W0);
          e.CSEL(dest_src, dest_src, W0, Cond::LO);
        });
  }
};
struct MIN_I32 : Sequence<MIN_I32, I<OPCODE_MIN, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](A64Emitter& e, WReg dest_src, WReg src) {
          e.CMP(dest_src, src);
          e.CSEL(dest_src, dest_src, src, Cond::LO);
        },
        [](A64Emitter& e, WReg dest_src, int32_t constant) {
          e.MOV(W0, constant);
          e.CMP(dest_src, W0);
          e.CSEL(dest_src, dest_src, W0, Cond::LO);
        });
  }
};
struct MIN_I64 : Sequence<MIN_I64, I<OPCODE_MIN, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](A64Emitter& e, XReg dest_src, XReg src) {
          e.CMP(dest_src, src);
          e.CSEL(dest_src, dest_src, src, Cond::LO);
        },
        [](A64Emitter& e, XReg dest_src, int64_t constant) {
          e.MOV(X0, constant);
          e.CMP(dest_src, X0);
          e.CSEL(dest_src, dest_src, X0, Cond::LO);
        });
  }
};
struct MIN_F32 : Sequence<MIN_F32, I<OPCODE_MIN, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp<SReg>(
        e, i, [](A64Emitter& e, SReg dest, SReg src1, SReg src2) {
          e.FMIN(dest, src1, src2);
        });
  }
};
struct MIN_F64 : Sequence<MIN_F64, I<OPCODE_MIN, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp<DReg>(
        e, i, [](A64Emitter& e, DReg dest, DReg src1, DReg src2) {
          e.FMIN(dest, src1, src2);
        });
  }
};
struct MIN_V128 : Sequence<MIN_V128, I<OPCODE_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      QReg src1 = Q0;
      QReg src2 = Q1;
      if (i.src1.is_constant) {
        e.LoadConstantV(src1, i.src1.constant());
      } else {
        e.MOV(src1.B16(), i.src1.reg().B16());
      }
      if (i.src2.is_constant) {
        e.LoadConstantV(src2, i.src2.constant());
      } else {
        e.MOV(src2.B16(), i.src2.reg().B16());
      }

      e.FMIN(Q2.S4(), src1.S4(), src2.S4());
      EmitSelectFirstNanLanesF32x4(e, Q2, src1, src2);
      e.MOV(i.dest.reg().B16(), Q2.B16());
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MIN, MIN_I8, MIN_I16, MIN_I32, MIN_I64, MIN_F32,
                     MIN_F64, MIN_V128);

// ============================================================================
// OPCODE_SELECT
// ============================================================================
// dest = src1 ? src2 : src3
// TODO(benvanik): match compare + select sequences, as often it's something
//     like SELECT(VECTOR_COMPARE_SGE(a, b), a, b)
struct SELECT_I8
    : Sequence<SELECT_I8, I<OPCODE_SELECT, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg src2(0);
    if (i.src2.is_constant) {
      src2 = W0;
      e.MOV(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.CMP(i.src1.reg().toX(), 0);
    e.CSEL(i.dest, src2, i.src3, Cond::NE);
  }
};
struct SELECT_I16
    : Sequence<SELECT_I16, I<OPCODE_SELECT, I16Op, I8Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg src2(0);
    if (i.src2.is_constant) {
      src2 = W0;
      e.MOV(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.CMP(i.src1.reg().toX(), 0);
    e.CSEL(i.dest, src2, i.src3, Cond::NE);
  }
};
struct SELECT_I32
    : Sequence<SELECT_I32, I<OPCODE_SELECT, I32Op, I8Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg src2(0);
    if (i.src2.is_constant) {
      src2 = W0;
      e.MOV(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.CMP(i.src1.reg().toX(), 0);
    e.CSEL(i.dest, src2, i.src3, Cond::NE);
  }
};
struct SELECT_I64
    : Sequence<SELECT_I64, I<OPCODE_SELECT, I64Op, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    XReg src2(0);
    if (i.src2.is_constant) {
      src2 = X0;
      e.MOV(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.CMP(i.src1.reg().toX(), 0);
    e.CSEL(i.dest, src2, i.src3, Cond::NE);
  }
};
struct SELECT_F32
    : Sequence<SELECT_F32, I<OPCODE_SELECT, F32Op, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 != 0 ? src2 : src3

    SReg src2 = i.src2.is_constant ? S2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    }

    SReg src3 = i.src3.is_constant ? S3 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantV(src3.toQ(), i.src3.constant());
    }

    e.CMP(i.src1.reg().toX(), 0);
    e.FCSEL(i.dest, src2, src3, Cond::NE);
  }
};
struct SELECT_F64
    : Sequence<SELECT_F64, I<OPCODE_SELECT, F64Op, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 != 0 ? src2 : src3

    const DReg src2 = i.src2.is_constant ? D2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    }

    const DReg src3 = i.src3.is_constant ? D3 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantV(src3.toQ(), i.src3.constant());
    }

    e.CMP(i.src1.reg().toX(), 0);
    e.FCSEL(i.dest, src2, src3, Cond::NE);
  }
};
struct SELECT_V128_I8
    : Sequence<SELECT_V128_I8, I<OPCODE_SELECT, V128Op, I8Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 != 0 ? src2 : src3

    const QReg src2 = i.src2.is_constant ? Q2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantV(src2, i.src2.constant());
    }

    const QReg src3 = i.src3.is_constant ? Q3 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantV(src3, i.src3.constant());
    }

    e.CMP(i.src1.reg().toX(), 0);
    e.CSETM(W0, Cond::NE);
    e.DUP(i.dest.reg().S4(), W0);
    e.BSL(i.dest.reg().B16(), src2.B16(), src3.B16());
  }
};
struct SELECT_V128_V128
    : Sequence<SELECT_V128_V128,
               I<OPCODE_SELECT, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    const QReg src1 = Q0;
    if (i.src1.is_constant) {
      e.LoadConstantV(src1, i.src1.constant());
    } else {
      e.MOV(src1.B16(), i.src1.reg().B16());
    }

    const QReg src2 = i.src2.is_constant ? Q2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantV(src2, i.src2.constant());
    }

    const QReg src3 = i.src3.is_constant ? Q3 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantV(src3, i.src3.constant());
    }

    // src1 ? src2 : src3;
    e.BSL(src1.B16(), src3.B16(), src2.B16());
    e.MOV(i.dest.reg().B16(), src1.B16());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SELECT, SELECT_I8, SELECT_I16, SELECT_I32,
                     SELECT_I64, SELECT_F32, SELECT_F64, SELECT_V128_I8,
                     SELECT_V128_V128);

// ============================================================================
// OPCODE_IS_NAN
// ============================================================================
struct IS_NAN_F32 : Sequence<IS_NAN_F32, I<OPCODE_IS_NAN, I8Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.FCMP(i.src1, i.src1);
    e.CSET(i.dest, Cond::VS);
  }
};

struct IS_NAN_F64 : Sequence<IS_NAN_F64, I<OPCODE_IS_NAN, I8Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.FCMP(i.src1, i.src1);
    e.CSET(i.dest, Cond::VS);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_NAN, IS_NAN_F32, IS_NAN_F64);

// ============================================================================
// OPCODE_COMPARE_EQ
// ============================================================================
struct COMPARE_EQ_I8
    : Sequence<COMPARE_EQ_I8, I<OPCODE_COMPARE_EQ, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i, [](A64Emitter& e, WReg src1, WReg src2) { e.CMP(src1, src2); },
        [](A64Emitter& e, WReg src1, int32_t constant) {
          e.MOV(W1, constant);
          e.CMP(src1, W1);
        });
    e.CSET(i.dest, Cond::EQ);
  }
};
struct COMPARE_EQ_I16
    : Sequence<COMPARE_EQ_I16, I<OPCODE_COMPARE_EQ, I8Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i, [](A64Emitter& e, WReg src1, WReg src2) { e.CMP(src1, src2); },
        [](A64Emitter& e, WReg src1, int32_t constant) {
          e.MOV(W1, constant);
          e.CMP(src1, W1);
        });
    e.CSET(i.dest, Cond::EQ);
  }
};
struct COMPARE_EQ_I32
    : Sequence<COMPARE_EQ_I32, I<OPCODE_COMPARE_EQ, I8Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i, [](A64Emitter& e, WReg src1, WReg src2) { e.CMP(src1, src2); },
        [](A64Emitter& e, WReg src1, int32_t constant) {
          e.MOV(W1, constant);
          e.CMP(src1, W1);
        });
    e.CSET(i.dest, Cond::EQ);
  }
};
struct COMPARE_EQ_I64
    : Sequence<COMPARE_EQ_I64, I<OPCODE_COMPARE_EQ, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i, [](A64Emitter& e, XReg src1, XReg src2) { e.CMP(src1, src2); },
        [](A64Emitter& e, XReg src1, int32_t constant) {
          e.MOV(X1, constant);
          e.CMP(src1, X1);
        });
    e.CSET(i.dest, Cond::EQ);
  }
};
struct COMPARE_EQ_F32
    : Sequence<COMPARE_EQ_F32, I<OPCODE_COMPARE_EQ, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp<SReg>(
        e, i, [](A64Emitter& e, I8Op dest, const SReg& src1, const SReg& src2) {
          e.FCMP(src1, src2);
        });
    e.CSET(i.dest, Cond::EQ);
  }
};
struct COMPARE_EQ_F64
    : Sequence<COMPARE_EQ_F64, I<OPCODE_COMPARE_EQ, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp<DReg>(
        e, i, [](A64Emitter& e, I8Op dest, const DReg& src1, const DReg& src2) {
          e.FCMP(src1, src2);
        });
    e.CSET(i.dest, Cond::EQ);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_EQ, COMPARE_EQ_I8, COMPARE_EQ_I16,
                     COMPARE_EQ_I32, COMPARE_EQ_I64, COMPARE_EQ_F32,
                     COMPARE_EQ_F64);

// ============================================================================
// OPCODE_COMPARE_NE
// ============================================================================
struct COMPARE_NE_I8
    : Sequence<COMPARE_NE_I8, I<OPCODE_COMPARE_NE, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i, [](A64Emitter& e, WReg src1, WReg src2) { e.CMP(src1, src2); },
        [](A64Emitter& e, WReg src1, int32_t constant) {
          e.MOV(W1, constant);
          e.CMP(src1, W1);
        });
    e.CSET(i.dest, Cond::NE);
  }
};
struct COMPARE_NE_I16
    : Sequence<COMPARE_NE_I16, I<OPCODE_COMPARE_NE, I8Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i, [](A64Emitter& e, WReg src1, WReg src2) { e.CMP(src1, src2); },
        [](A64Emitter& e, WReg src1, int32_t constant) {
          e.MOV(W1, constant);
          e.CMP(src1, W1);
        });
    e.CSET(i.dest, Cond::NE);
  }
};
struct COMPARE_NE_I32
    : Sequence<COMPARE_NE_I32, I<OPCODE_COMPARE_NE, I8Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i, [](A64Emitter& e, WReg src1, WReg src2) { e.CMP(src1, src2); },
        [](A64Emitter& e, WReg src1, int32_t constant) {
          e.MOV(W1, constant);
          e.CMP(src1, W1);
        });
    e.CSET(i.dest, Cond::NE);
  }
};
struct COMPARE_NE_I64
    : Sequence<COMPARE_NE_I64, I<OPCODE_COMPARE_NE, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i, [](A64Emitter& e, XReg src1, XReg src2) { e.CMP(src1, src2); },
        [](A64Emitter& e, XReg src1, int32_t constant) {
          e.MOV(X1, constant);
          e.CMP(src1, X1);
        });
    e.CSET(i.dest, Cond::NE);
  }
};
struct COMPARE_NE_F32
    : Sequence<COMPARE_NE_F32, I<OPCODE_COMPARE_NE, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.FCMP(i.src1, i.src2);
    e.CSET(i.dest, Cond::NE);
  }
};
struct COMPARE_NE_F64
    : Sequence<COMPARE_NE_F64, I<OPCODE_COMPARE_NE, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.FCMP(i.src1, i.src2);
    e.CSET(i.dest, Cond::NE);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_NE, COMPARE_NE_I8, COMPARE_NE_I16,
                     COMPARE_NE_I32, COMPARE_NE_I64, COMPARE_NE_F32,
                     COMPARE_NE_F64);

// ============================================================================
// OPCODE_COMPARE_*
// ============================================================================
#define EMITTER_ASSOCIATIVE_COMPARE_INT(op, cond, inverse_cond, type,          \
                                        reg_type)                              \
  struct COMPARE_##op##_##type                                                 \
      : Sequence<COMPARE_##op##_##type,                                        \
                 I<OPCODE_COMPARE_##op, I8Op, type, type>> {                   \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      EmitAssociativeCompareOp(                                                \
          e, i,                                                                \
          [](A64Emitter& e, WReg dest, const reg_type& src1,                   \
             const reg_type& src2, bool inverse) {                             \
            e.CMP(src1, src2);                                                 \
            if (!inverse) {                                                    \
              e.CSET(dest, cond);                                              \
            } else {                                                           \
              e.CSET(dest, inverse_cond);                                      \
            }                                                                  \
          },                                                                   \
          [](A64Emitter& e, WReg dest, const reg_type& src1, int32_t constant, \
             bool inverse) {                                                   \
            e.MOV(reg_type(1), constant);                                      \
            e.CMP(src1, reg_type(1));                                          \
            if (!inverse) {                                                    \
              e.CSET(dest, cond);                                              \
            } else {                                                           \
              e.CSET(dest, inverse_cond);                                      \
            }                                                                  \
          });                                                                  \
    }                                                                          \
  };
#define EMITTER_ASSOCIATIVE_COMPARE_XX(op, cond, inverse_cond)          \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, cond, inverse_cond, I8Op, WReg);  \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, cond, inverse_cond, I16Op, WReg); \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, cond, inverse_cond, I32Op, WReg); \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, cond, inverse_cond, I64Op, XReg); \
  EMITTER_OPCODE_TABLE(OPCODE_COMPARE_##op, COMPARE_##op##_I8Op,        \
                       COMPARE_##op##_I16Op, COMPARE_##op##_I32Op,      \
                       COMPARE_##op##_I64Op);
EMITTER_ASSOCIATIVE_COMPARE_XX(SLT, Cond::LT, Cond::GT);  // setl, setg
EMITTER_ASSOCIATIVE_COMPARE_XX(SLE, Cond::LE, Cond::GE);  // setle, setge
EMITTER_ASSOCIATIVE_COMPARE_XX(SGT, Cond::GT, Cond::LT);  // setg, setl
EMITTER_ASSOCIATIVE_COMPARE_XX(SGE, Cond::GE, Cond::LE);  // setge, setle
EMITTER_ASSOCIATIVE_COMPARE_XX(ULT, Cond::LO, Cond::HI);  // setb, seta
EMITTER_ASSOCIATIVE_COMPARE_XX(ULE, Cond::LS, Cond::HS);  // setbe, setae
EMITTER_ASSOCIATIVE_COMPARE_XX(UGE, Cond::HS, Cond::LS);  // setae, setbe
EMITTER_ASSOCIATIVE_COMPARE_XX(UGT, Cond::HI, Cond::LO);  // seta, setb

// https://web.archive.org/web/20171129015931/https://x86.renejeschke.de/html/file_module_x86_id_288.html
// Original link: https://x86.renejeschke.de/html/file_module_x86_id_288.html
#define EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(op, cond)                  \
  struct COMPARE_##op##_F32                                           \
      : Sequence<COMPARE_##op##_F32,                                  \
                 I<OPCODE_COMPARE_##op, I8Op, F32Op, F32Op>> {        \
    static void Emit(A64Emitter& e, const EmitArgType& i) {           \
      e.FCMP(i.src1, i.src2);                                         \
      e.CSET(i.dest, cond);                                           \
    }                                                                 \
  };                                                                  \
  struct COMPARE_##op##_F64                                           \
      : Sequence<COMPARE_##op##_F64,                                  \
                 I<OPCODE_COMPARE_##op, I8Op, F64Op, F64Op>> {        \
    static void Emit(A64Emitter& e, const EmitArgType& i) {           \
      if (i.src1.is_constant) {                                       \
        e.LoadConstantV(Q0, i.src1.constant());                       \
        e.FCMP(D0, i.src2);                                           \
      } else if (i.src2.is_constant) {                                \
        e.LoadConstantV(Q0, i.src2.constant());                       \
        e.FCMP(i.src1, D0);                                           \
      } else {                                                        \
        e.FCMP(i.src1, i.src2);                                       \
      }                                                               \
      e.CSET(i.dest, cond);                                           \
    }                                                                 \
  };                                                                  \
  EMITTER_OPCODE_TABLE(OPCODE_COMPARE_##op##_FLT, COMPARE_##op##_F32, \
                       COMPARE_##op##_F64);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SLT, Cond::LT);  // setb
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SLE, Cond::LE);  // setbe
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SGT, Cond::GT);  // seta
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SGE, Cond::GE);  // setae
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(ULT, Cond::LO);  // setb
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(ULE, Cond::LS);  // setbe
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(UGT, Cond::HI);  // seta
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(UGE, Cond::HS);  // setae

// ============================================================================
// OPCODE_DID_SATURATE
// ============================================================================
struct DID_SATURATE
    : Sequence<DID_SATURATE, I<OPCODE_DID_SATURATE, I8Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Bit 27 in the FPSR is the QC bit
    e.MRS(X0, SystemReg::FPSR);
    e.UBFX(i.dest, W0, 27, 1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DID_SATURATE, DID_SATURATE);

// ============================================================================
// OPCODE_ADD
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAddXX(A64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](A64Emitter& e, REG dest_src, REG src) {
        e.ADD(dest_src, dest_src, src);
      },
      [](A64Emitter& e, REG dest_src, int32_t constant) {
        e.MOV(REG(1), constant);
        e.ADD(dest_src, dest_src, REG(1));
      });
}
struct ADD_I8 : Sequence<ADD_I8, I<OPCODE_ADD, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I8, WReg>(e, i);
  }
};
struct ADD_I16 : Sequence<ADD_I16, I<OPCODE_ADD, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I16, WReg>(e, i);
  }
};
struct ADD_I32 : Sequence<ADD_I32, I<OPCODE_ADD, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I32, WReg>(e, i);
  }
};
struct ADD_I64 : Sequence<ADD_I64, I<OPCODE_ADD, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I64, XReg>(e, i);
  }
};
struct ADD_F32 : Sequence<ADD_F32, I<OPCODE_ADD, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp<SReg>(
        e, i, [](A64Emitter& e, SReg dest, SReg src1, SReg src2) {
          e.FADD(dest, src1, src2);
        });
  }
};
struct ADD_F64 : Sequence<ADD_F64, I<OPCODE_ADD, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg src1 = i.src1.is_constant ? D1 : i.src1.reg();
    DReg src2 = i.src2.is_constant ? D2 : i.src2.reg();
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    }
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    }

    oaknut::Label do_op;
    oaknut::Label done;
    EmitPropagateInputNanF64Binary(e, i.dest.reg(), src1, src2, do_op, done);
    e.l(do_op);
    e.FADD(i.dest.reg(), src1, src2);
    e.l(done);
  }
};
struct ADD_V128 : Sequence<ADD_V128, I<OPCODE_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      EmitCommutativeBinaryVOp(
          e, i, [](A64Emitter& e, QReg dest, QReg src1, QReg src2) {
            e.FADD(dest.S4(), src1.S4(), src2.S4());
          });
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD, ADD_I8, ADD_I16, ADD_I32, ADD_I64, ADD_F32,
                     ADD_F64, ADD_V128);

// ============================================================================
// OPCODE_ADD_CARRY
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAddCarryXX(A64Emitter& e, const ARGS& i) {
  // TODO(benvanik): faster setting? we could probably do some fun math tricks
  // here to get the carry flag set.
  if (i.src3.is_constant) {
    e.MOV(W0, WZR);
    if (i.src3.constant()) {
      // Set carry
      // This is implicitly "SUBS 0 - 0"
      e.CMP(W0, 0);
    } else {
      // Clear carry
      e.CMN(W0, 0);
    }
  } else {
    // If src3 is non-zero, set the carry flag
    e.CMP(i.src3.reg().toW(), 0);
    e.CSET(X0, Cond::NE);

    e.MRS(X1, SystemReg::NZCV);
    // Assign carry bit
    e.BFI(X1, X0, 29, 1);
    e.MSR(SystemReg::NZCV, X1);
  }
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](A64Emitter& e, const REG& dest_src, const REG& src) {
        e.ADC(dest_src, dest_src, src);
      },
      [](A64Emitter& e, const REG& dest_src, int32_t constant) {
        e.MOV(REG(1), constant);
        e.ADC(dest_src, dest_src, REG(1));
      });
}
struct ADD_CARRY_I8
    : Sequence<ADD_CARRY_I8, I<OPCODE_ADD_CARRY, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I8, WReg>(e, i);
  }
};
struct ADD_CARRY_I16
    : Sequence<ADD_CARRY_I16, I<OPCODE_ADD_CARRY, I16Op, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I16, WReg>(e, i);
  }
};
struct ADD_CARRY_I32
    : Sequence<ADD_CARRY_I32, I<OPCODE_ADD_CARRY, I32Op, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I32, WReg>(e, i);
  }
};
struct ADD_CARRY_I64
    : Sequence<ADD_CARRY_I64, I<OPCODE_ADD_CARRY, I64Op, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I64, XReg>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD_CARRY, ADD_CARRY_I8, ADD_CARRY_I16,
                     ADD_CARRY_I32, ADD_CARRY_I64);

// ============================================================================
// OPCODE_SUB
// ============================================================================
template <typename T, typename REG>
static void EmitSubInt(A64Emitter& e, const T& i) {
  if (i.src1.is_constant && i.src2.is_constant) {
    e.mov(
        i.dest,
        static_cast<uint64_t>(
            static_cast<
                typename std::make_unsigned<decltype(i.src1.constant())>::type>(
                i.src1.constant() - i.src2.constant())));
  } else if (i.src2.is_constant) {
    uint64_t imm = static_cast<uint64_t>(
        static_cast<
            typename std::make_unsigned<decltype(i.src2.constant())>::type>(
            i.src2.constant()));
    if (imm <= 4095) {
      e.sub(i.dest, i.src1, static_cast<uint32_t>(imm));
    } else {
      e.mov(e.w0, static_cast<uint64_t>(imm));
      e.sub(i.dest, i.src1, REG(0));
    }
  } else if (i.src1.is_constant) {
    uint64_t imm = static_cast<uint64_t>(
        static_cast<
            typename std::make_unsigned<decltype(i.src1.constant())>::type>(
            i.src1.constant()));
    // Use scratch register to avoid clobbering src2 when dest == src2.
    e.mov(e.w17, imm);
    e.sub(i.dest, REG(17), i.src2);
  } else {
    e.sub(i.dest, i.src1, i.src2);
  }
}
struct SUB_I8 : Sequence<SUB_I8, I<OPCODE_SUB, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSubInt<EmitArgType, WReg>(e, i);
  }
};
struct SUB_I16 : Sequence<SUB_I16, I<OPCODE_SUB, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSubInt<EmitArgType, WReg>(e, i);
  }
};
struct SUB_I32 : Sequence<SUB_I32, I<OPCODE_SUB, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSubInt<EmitArgType, WReg>(e, i);
  }
};
struct SUB_I64 : Sequence<SUB_I64, I<OPCODE_SUB, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() - i.src2.constant()));
    } else if (i.src2.is_constant) {
      uint64_t imm = static_cast<uint64_t>(i.src2.constant());
      if (imm <= 4095) {
        e.sub(i.dest, i.src1, static_cast<uint32_t>(imm));
      } else {
        e.mov(e.x0, imm);
        e.sub(i.dest, i.src1, e.x0);
      }
    } else if (i.src1.is_constant) {
      // Use scratch register to avoid clobbering src2 when dest == src2.
      e.mov(e.x17, static_cast<uint64_t>(i.src1.constant()));
      e.sub(i.dest, e.x17, i.src2);
    } else {
      e.sub(i.dest, i.src1, i.src2);
    }
  }
};
struct SUB_F32 : Sequence<SUB_F32, I<OPCODE_SUB, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    EmitFpBinOpWithPpcNan_F32(e, i.dest, s1, s2, FpBinOp::Sub);
  }
};
struct SUB_F64 : Sequence<SUB_F64, I<OPCODE_SUB, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    DReg src1 = i.src1.is_constant ? D1 : i.src1.reg();
    DReg src2 = i.src2.is_constant ? D2 : i.src2.reg();
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    }
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    }

    oaknut::Label do_op;
    oaknut::Label done;
    EmitPropagateInputNanF64Binary(e, i.dest.reg(), src1, src2, do_op, done);
    e.l(do_op);
    e.FSUB(i.dest.reg(), src1, src2);
    e.l(done);
  }
};
struct SUB_V128 : Sequence<SUB_V128, I<OPCODE_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitWithVmxFpcr(e, [&] {
      EmitAssociativeBinaryVOp(
          e, i, [](A64Emitter& e, QReg dest, QReg src1, QReg src2) {
            e.FSUB(dest.S4(), src1.S4(), src2.S4());
          });
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SUB, SUB_I8, SUB_I16, SUB_I32, SUB_I64, SUB_F32,
                     SUB_F64, SUB_V128);

// ============================================================================
// OPCODE_ADD_CARRY
// ============================================================================
struct ADD_CARRY_I8
    : Sequence<ADD_CARRY_I8, I<OPCODE_ADD_CARRY, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 + src2 + src3 (carry in)
    if (i.src1.is_constant && i.src2.is_constant && i.src3.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(
                (i.src1.constant() + i.src2.constant() + i.src3.constant()) &
                0xFF));
    } else {
      // Load src1 into dest (or w0 if constant).
      if (i.src1.is_constant) {
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      } else {
        e.mov(e.w0, i.src1);
      }
      // Add src2.
      if (i.src2.is_constant) {
        e.add(e.w0, e.w0, static_cast<uint32_t>(i.src2.constant() & 0xFF));
      } else {
        e.add(e.w0, e.w0, i.src2);
      }
      // Add carry.
      if (i.src3.is_constant) {
        if (i.src3.constant()) {
          e.add(e.w0, e.w0, 1);
        }
      } else {
        e.add(e.w0, e.w0, i.src3);
      }
      e.mov(i.dest, e.w0);
    }
  }
};
struct ADD_CARRY_I16
    : Sequence<ADD_CARRY_I16, I<OPCODE_ADD_CARRY, I16Op, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
      e.add(e.w0, e.w0, e.w1);
    } else {
      e.add(e.w0, e.w0, i.src2);
    }
    if (i.src3.is_constant) {
      if (i.src3.constant()) e.add(e.w0, e.w0, 1);
    } else {
      e.add(e.w0, e.w0, i.src3);
    }
    e.mov(i.dest, e.w0);
  }
};
struct ADD_CARRY_I32
    : Sequence<ADD_CARRY_I32, I<OPCODE_ADD_CARRY, I32Op, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    } else {
      e.mov(e.w0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.w1,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      e.add(e.w0, e.w0, e.w1);
    } else {
      e.add(e.w0, e.w0, i.src2);
    }
    if (i.src3.is_constant) {
      if (i.src3.constant()) e.add(e.w0, e.w0, 1);
    } else {
      e.add(e.w0, e.w0, i.src3);
    }
    e.mov(i.dest, e.w0);
  }
};
struct ADD_CARRY_I64
    : Sequence<ADD_CARRY_I64, I<OPCODE_ADD_CARRY, I64Op, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
    } else {
      e.mov(e.x0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));
      e.add(e.x0, e.x0, e.x1);
    } else {
      e.add(e.x0, e.x0, i.src2);
    }
    if (i.src3.is_constant) {
      if (i.src3.constant()) e.add(e.x0, e.x0, 1);
    } else {
      // Zero-extend the I8 carry to 64-bit.
      e.mov(e.w1, i.src3);
      e.uxtb(e.w1, e.w1);
      e.add(e.x0, e.x0, e.x1);
    }
    e.mov(i.dest, e.x0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD_CARRY, ADD_CARRY_I8, ADD_CARRY_I16,
                     ADD_CARRY_I32, ADD_CARRY_I64);

// ============================================================================
// OPCODE_MUL
// ============================================================================
struct MUL_I32 : Sequence<MUL_I32, I<OPCODE_MUL, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() * i.src2.constant())));
    } else if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      e.mul(i.dest, e.w0, i.src2);
    } else if (i.src2.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      e.mul(i.dest, i.src1, e.w0);
    } else {
      e.mul(i.dest, i.src1, i.src2);
    }
  }
};
struct MUL_I64 : Sequence<MUL_I64, I<OPCODE_MUL, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() * i.src2.constant()));
    } else if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.mul(i.dest, e.x0, i.src2);
    } else if (i.src2.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
      e.mul(i.dest, i.src1, e.x0);
    } else {
      e.mul(i.dest, i.src1, i.src2);
    }
  }
};
struct MUL_F32 : Sequence<MUL_F32, I<OPCODE_MUL, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    EmitFpBinOpWithPpcNan_F32(e, i.dest, s1, s2, FpBinOp::Mul);
  }
};
struct MUL_F64 : Sequence<MUL_F64, I<OPCODE_MUL, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    DReg src1 = i.src1.is_constant ? D1 : i.src1.reg();
    DReg src2 = i.src2.is_constant ? D2 : i.src2.reg();
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    }
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    }

    oaknut::Label do_op;
    oaknut::Label done;
    EmitPropagateInputNanF64Binary(e, i.dest.reg(), src1, src2, do_op, done);
    e.l(do_op);
    e.FMUL(i.dest.reg(), src1, src2);
    e.l(done);
  }
};
struct MUL_V128 : Sequence<MUL_V128, I<OPCODE_MUL, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitWithVmxFpcr(e, [&] {
      EmitCommutativeBinaryVOp(
          e, i, [](A64Emitter& e, QReg dest, QReg src1, QReg src2) {
            e.FMUL(dest.S4(), src1.S4(), src2.S4());
          });
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL, MUL_I32, MUL_I64, MUL_F32, MUL_F64, MUL_V128);

// ============================================================================
// OPCODE_MUL_HI
// ============================================================================
struct MUL_HI_I64
    : Sequence<MUL_HI_I64, I<OPCODE_MUL_HI, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    XReg s1 = i.src1.is_constant ? e.x0 : XReg(i.src1.reg().getIdx());
    XReg s2 = i.src2.is_constant ? e.x1 : XReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
    }
    if (i.src2.is_constant) {
      e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));
    }
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.umulh(i.dest, s1, s2);
    } else {
      e.smulh(i.dest, s1, s2);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_HI, MUL_HI_I64);

// ============================================================================
// OPCODE_DIV
// ============================================================================
struct DIV_I32 : Sequence<DIV_I32, I<OPCODE_DIV, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // ARM64 sdiv/udiv returns 0 on divide by zero (no exception).
    WReg s1 = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    WReg s2 = i.src2.is_constant ? e.w1 : WReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    }
    if (i.src2.is_constant) {
      e.mov(e.w1,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    }
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (i.src2.is_constant) {
        if (i.src2.constant() == 0) {
          e.MOV(i.dest, 0);
          return;
        }
        assert_true(!i.src1.is_constant);
        e.MOV(W0, i.src2.constant());
        e.UDIV(i.dest, i.src1, W0);
      } else {
        oaknut::Label div_zero, done;
        e.CBZ(i.src2, div_zero);
        if (i.src1.is_constant) {
          e.MOV(W0, i.src1.constant());
          e.UDIV(i.dest, W0, i.src2);
        } else {
          e.UDIV(i.dest, i.src1, i.src2);
        }
        e.B(done);
        e.l(div_zero);
        e.MOV(i.dest, 0);
        e.l(done);
      }
    } else {
      if (i.src2.is_constant) {
        const int32_t divisor = i.src2.constant();
        if (divisor == 0) {
          e.MOV(i.dest, 0);
          return;
        }
        assert_true(!i.src1.is_constant);
        if (divisor == -1) {
          oaknut::Label do_div, done;
          e.MOV(W1, 0x80000000u);
          e.CMP(i.src1, W1);
          e.B(Cond::NE, do_div);
          e.MOV(i.dest, 0);
          e.B(done);
          e.l(do_div);
          e.MOV(W0, divisor);
          e.SDIV(i.dest, i.src1, W0);
          e.l(done);
          return;
        }
        e.MOV(W0, divisor);
        e.SDIV(i.dest, i.src1, W0);
      } else {
        oaknut::Label div_zero, do_div, done;
        e.CBZ(i.src2, div_zero);
        e.MOV(W2, static_cast<uint32_t>(-1));
        e.CMP(i.src2, W2);
        e.B(Cond::NE, do_div);
        e.MOV(W1, 0x80000000u);
        if (i.src1.is_constant) {
          e.MOV(W0, i.src1.constant());
          e.CMP(W0, W1);
        } else {
          e.CMP(i.src1, W1);
        }
        e.B(Cond::NE, do_div);
        e.MOV(i.dest, 0);
        e.B(done);
        e.l(do_div);
        if (i.src1.is_constant) {
          e.MOV(W0, i.src1.constant());
          e.SDIV(i.dest, W0, i.src2);
        } else {
          e.SDIV(i.dest, i.src1, i.src2);
        }
        e.B(done);
        e.l(div_zero);
        e.MOV(i.dest, 0);
        e.l(done);
      }
    }
  }
};
struct DIV_I64 : Sequence<DIV_I64, I<OPCODE_DIV, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    XReg s1 = i.src1.is_constant ? e.x0 : XReg(i.src1.reg().getIdx());
    XReg s2 = i.src2.is_constant ? e.x1 : XReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
    }
    if (i.src2.is_constant) {
      e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));
    }
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (i.src2.is_constant) {
        if (i.src2.constant() == 0) {
          e.MOV(i.dest, 0);
          return;
        }
        assert_true(!i.src1.is_constant);
        e.MOV(X0, i.src2.constant());
        e.UDIV(i.dest, i.src1, X0);
      } else {
        oaknut::Label div_zero, done;
        e.CBZ(i.src2, div_zero);
        if (i.src1.is_constant) {
          e.MOV(X0, i.src1.constant());
          e.UDIV(i.dest, X0, i.src2);
        } else {
          e.UDIV(i.dest, i.src1, i.src2);
        }
        e.B(done);
        e.l(div_zero);
        e.MOV(i.dest, 0);
        e.l(done);
      }
    } else {
      if (i.src2.is_constant) {
        const int64_t divisor = i.src2.constant();
        if (divisor == 0) {
          e.MOV(i.dest, 0);
          return;
        }
        assert_true(!i.src1.is_constant);
        if (divisor == -1) {
          oaknut::Label do_div, done;
          e.MOV(X1, 0x8000000000000000ull);
          e.CMP(i.src1, X1);
          e.B(Cond::NE, do_div);
          e.MOV(i.dest, 0);
          e.B(done);
          e.l(do_div);
          e.MOV(X0, divisor);
          e.SDIV(i.dest, i.src1, X0);
          e.l(done);
          return;
        }
        e.MOV(X0, divisor);
        e.SDIV(i.dest, i.src1, X0);
      } else {
        oaknut::Label div_zero, do_div, done;
        e.CBZ(i.src2, div_zero);
        e.MOV(X2, static_cast<uint64_t>(-1));
        e.CMP(i.src2, X2);
        e.B(Cond::NE, do_div);
        e.MOV(X1, 0x8000000000000000ull);
        if (i.src1.is_constant) {
          e.MOV(X0, i.src1.constant());
          e.CMP(X0, X1);
        } else {
          e.CMP(i.src1, X1);
        }
        e.B(Cond::NE, do_div);
        e.MOV(i.dest, 0);
        e.B(done);
        e.l(do_div);
        if (i.src1.is_constant) {
          e.MOV(X0, i.src1.constant());
          e.SDIV(i.dest, X0, i.src2);
        } else {
          e.SDIV(i.dest, i.src1, i.src2);
        }
        e.B(done);
        e.l(div_zero);
        e.MOV(i.dest, 0);
        e.l(done);
      }
    }
  }
};
struct DIV_F32 : Sequence<DIV_F32, I<OPCODE_DIV, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    EmitFpBinOpWithPpcNan_F32(e, i.dest, s1, s2, FpBinOp::Div);
  }
};
struct DIV_F64 : Sequence<DIV_F64, I<OPCODE_DIV, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    DReg src1 = i.src1.is_constant ? D1 : i.src1.reg();
    DReg src2 = i.src2.is_constant ? D2 : i.src2.reg();
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    }
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    }

    oaknut::Label do_op;
    oaknut::Label done;
    EmitPropagateInputNanF64Binary(e, i.dest.reg(), src1, src2, do_op, done);
    e.l(do_op);
    e.FDIV(i.dest.reg(), src1, src2);
    e.l(done);
  }
};
struct DIV_V128 : Sequence<DIV_V128, I<OPCODE_DIV, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVmxFpBinOp_V128(e, i.dest.reg().getIdx(), i.src1, i.src2,
                        VmxFpBinOp::Div);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DIV, DIV_I8, DIV_I16, DIV_I32, DIV_I64, DIV_F32,
                     DIV_F64, DIV_V128);

// ============================================================================
// OPCODE_MUL_ADD
// ============================================================================
// d = 1 * 2 + 3
// $0 = $1x$0 + $2
struct MUL_ADD_F32
    : Sequence<MUL_ADD_F32, I<OPCODE_MUL_ADD, F32Op, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg src3 = S3;
    if (i.src3.is_constant) {
      e.LoadConstantV(src3.toQ(), i.src3.constant());
    } else {
      src3 = i.src3.reg();
    }

    SReg src2 = S2;
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    } else {
      src2 = i.src2.reg();
    }

    SReg src1 = S1;
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    } else {
      src1 = i.src1.reg();
    }

    e.FMADD(i.dest, src1, src2, src3);
  }
};
struct MUL_ADD_F64
    : Sequence<MUL_ADD_F64, I<OPCODE_MUL_ADD, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg src1 = D1;
    DReg src2 = D2;
    DReg src3 = D3;
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    } else {
      e.FMOV(src1, i.src1.reg());
    }
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    } else {
      e.FMOV(src2, i.src2.reg());
    }
    if (i.src3.is_constant) {
      e.LoadConstantV(src3.toQ(), i.src3.constant());
    } else {
      e.FMOV(src3, i.src3.reg());
    }

    oaknut::Label do_op;
    oaknut::Label done;
    EmitPropagateInputNanF64Ternary(e, i.dest.reg(), src1, src2, src3, do_op,
                                    done);
    e.l(do_op);
    e.FMADD(i.dest.reg(), src1, src2, src3);
    e.l(done);
  }
};
struct MUL_ADD_V128
    : Sequence<MUL_ADD_V128,
               I<OPCODE_MUL_ADD, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      const QReg dest = i.dest.reg();
      const QReg temp1 = Q0;
      const QReg temp2 = Q1;
      const QReg temp3 = Q2;

      if (i.src1.is_constant) {
        e.LoadConstantV(temp1, i.src1.constant());
      } else {
        e.MOV(temp1.B16(), i.src1.reg().B16());
      }

      if (i.src2.is_constant) {
        e.LoadConstantV(temp2, i.src2.constant());
      } else {
        e.MOV(temp2.B16(), i.src2.reg().B16());
      }

      if (i.src3.is_constant) {
        e.LoadConstantV(temp3, i.src3.constant());
      } else {
        e.MOV(temp3.B16(), i.src3.reg().B16());
      }

      e.MOV(dest.B16(), temp3.B16());
      e.FMLA(dest.S4(), temp1.S4(), temp2.S4());

      EmitSelectFirstQNanLanesF32x4(e, dest, temp1, temp2, temp3);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_ADD, MUL_ADD_F32, MUL_ADD_F64, MUL_ADD_V128);

// ============================================================================
// OPCODE_MUL_SUB
// ============================================================================
// d = 1 * 2 - 3
// $0 = $2x$0 - $3
// TODO(benvanik): use other forms (132/213/etc) to avoid register shuffling.
// dest could be src2 or src3 - need to ensure it's not before overwriting dest
// perhaps use other 132/213/etc
// Forms:
// - 132 -> $1 = $1 * $3 - $2
// - 213 -> $1 = $2 * $1 - $3
// - 231 -> $1 = $2 * $3 - $1
struct MUL_SUB_F32
    : Sequence<MUL_SUB_F32, I<OPCODE_MUL_SUB, F32Op, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg src3(1);
    if (i.src3.is_constant) {
      src3 = S1;
      e.LoadConstantV(src3.toQ(), i.src3.constant());
    } else {
      // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
      src3 = i.src3.reg();
      if (i.dest.reg().index() == i.src3.reg().index()) {
        e.FMOV(S1, i.src3);
        src3 = S1;
      }
    }

    // Multiply operation is commutative.
    EmitCommutativeBinaryVOp<SReg>(
        e, i, [](A64Emitter& e, SReg dest, SReg src1, SReg src2) {
          e.FMUL(dest, src1, src2);  // $0 = $1 * $2
        });

    e.FSUB(i.dest, i.dest, src3);  // $0 = $1 - $2
  }
};
struct MUL_SUB_F64
    : Sequence<MUL_SUB_F64, I<OPCODE_MUL_SUB, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg src1 = D1;
    DReg src2 = D2;
    DReg src3 = D3;
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    } else {
      e.FMOV(src1, i.src1.reg());
    }
    if (i.src2.is_constant) {
      e.LoadConstantV(src2.toQ(), i.src2.constant());
    } else {
      e.FMOV(src2, i.src2.reg());
    }
    if (i.src3.is_constant) {
      e.LoadConstantV(src3.toQ(), i.src3.constant());
    } else {
      e.FMOV(src3, i.src3.reg());
    }

    oaknut::Label do_op;
    oaknut::Label done;
    EmitPropagateInputNanF64Ternary(e, i.dest.reg(), src1, src2, src3, do_op,
                                    done);
    e.l(do_op);
    e.FMUL(i.dest.reg(), src1, src2);
    e.FSUB(i.dest.reg(), i.dest.reg(), src3);
    e.l(done);
  }
};
struct MUL_SUB_V128
    : Sequence<MUL_SUB_V128,
               I<OPCODE_MUL_SUB, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      const QReg dest = i.dest.reg();
      QReg temp1 = Q0;
      QReg temp2 = Q1;
      QReg temp3 = Q2;

      if (i.src1.is_constant) {
        e.LoadConstantV(temp1, i.src1.constant());
      } else {
        e.MOV(temp1.B16(), i.src1.reg().B16());
      }

      if (i.src2.is_constant) {
        e.LoadConstantV(temp2, i.src2.constant());
      } else {
        e.MOV(temp2.B16(), i.src2.reg().B16());
      }

      if (i.src3.is_constant) {
        e.LoadConstantV(temp3, i.src3.constant());
      } else {
        e.MOV(temp3.B16(), i.src3.reg().B16());
      }

      // a * b - c == a * b + (-c), keeping one final rounding.
      e.FNEG(dest.S4(), temp3.S4());
      e.FMLA(dest.S4(), temp1.S4(), temp2.S4());
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_SUB, MUL_SUB_F32, MUL_SUB_F64, MUL_SUB_V128);

// ============================================================================
// OPCODE_NEG
// ============================================================================
struct NEG_I8 : Sequence<NEG_I8, I<OPCODE_NEG, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint8_t>(-i.src1.constant())));
    } else {
      e.neg(i.dest, i.src1);
    }
  }
};
struct NEG_I16 : Sequence<NEG_I16, I<OPCODE_NEG, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint16_t>(-i.src1.constant())));
    } else {
      e.neg(i.dest, i.src1);
    }
  }
};
struct NEG_I32 : Sequence<NEG_I32, I<OPCODE_NEG, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint32_t>(-i.src1.constant())));
    } else {
      e.neg(i.dest, i.src1);
    }
  }
};
struct NEG_I64 : Sequence<NEG_I64, I<OPCODE_NEG, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(-i.src1.constant()));
    } else {
      e.neg(i.dest, i.src1);
    }
  }
};
struct NEG_F32 : Sequence<NEG_F32, I<OPCODE_NEG, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = -i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(i.dest, e.w0);
    } else {
      e.fneg(i.dest, i.src1);
    }
  }
};
struct NEG_F64 : Sequence<NEG_F64, I<OPCODE_NEG, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // PPC fnmadd/fnmsub are lowered to NEG(MUL_ADD/MUL_SUB).
    // For NaN results, PPC preserves sign/payload instead of negating them.
    if (!i.src1.is_constant && IsMulAddOrMulSubDef(i.src1.value)) {
      DReg src_for_nan = i.src1.reg();
      if (i.dest.reg().index() == i.src1.reg().index()) {
        src_for_nan = i.src1.reg().index() == 0 ? D1 : D0;
        e.FMOV(src_for_nan, i.src1.reg());
      }

      oaknut::Label done;
      e.FNEG(i.dest, i.src1);
      e.FCMP(src_for_nan, src_for_nan);
      e.B(Cond::VC, done);
      e.FMOV(i.dest.reg(), src_for_nan);
      e.l(done);
      return;
    }

    e.FNEG(i.dest, i.src1);
  }
};
struct NEG_V128 : Sequence<NEG_V128, I<OPCODE_NEG, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s = SrcVReg(e, i.src1, 0);
      e.fneg(VReg(i.dest.reg().getIdx()).s4, VReg(s).s4);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NEG, NEG_I8, NEG_I16, NEG_I32, NEG_I64, NEG_F32,
                     NEG_F64, NEG_V128);

// ============================================================================
// OPCODE_ABS
// ============================================================================
struct ABS_F32 : Sequence<ABS_F32, I<OPCODE_ABS, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      c.u &= 0x7FFFFFFF;
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(i.dest, e.w0);
    } else {
      e.fabs(i.dest, i.src1);
    }
  }
};
struct ABS_F64 : Sequence<ABS_F64, I<OPCODE_ABS, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      c.u &= 0x7FFFFFFFFFFFFFFFULL;
      e.mov(e.x0, c.u);
      e.fmov(i.dest, e.x0);
    } else {
      e.fabs(i.dest, i.src1);
    }
  }
};
struct ABS_V128 : Sequence<ABS_V128, I<OPCODE_ABS, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s = SrcVReg(e, i.src1, 0);
      e.fabs(VReg(i.dest.reg().getIdx()).s4, VReg(s).s4);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ABS, ABS_F32, ABS_F64, ABS_V128);

// ============================================================================
// OPCODE_AND
// ============================================================================
struct AND_I8 : Sequence<AND_I8, I<OPCODE_AND, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() & i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      e.and_imm(i.dest, i.src1, i.src2.constant() & 0xFF, e.w0);
    } else if (i.src1.is_constant) {
      e.and_imm(i.dest, i.src2, i.src1.constant() & 0xFF, e.w0);
    } else {
      e.and_(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_I16 : Sequence<AND_I16, I<OPCODE_AND, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() & i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      e.and_imm(i.dest, i.src1, i.src2.constant() & 0xFFFF, e.w0);
    } else if (i.src1.is_constant) {
      e.and_imm(i.dest, i.src2, i.src1.constant() & 0xFFFF, e.w0);
    } else {
      e.and_(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_I32 : Sequence<AND_I32, I<OPCODE_AND, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() & i.src2.constant())));
    } else if (i.src2.is_constant) {
      e.and_imm(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant()), e.w0);
    } else if (i.src1.is_constant) {
      e.and_imm(i.dest, i.src2, static_cast<uint32_t>(i.src1.constant()), e.w0);
    } else {
      e.and_(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_I64 : Sequence<AND_I64, I<OPCODE_AND, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() & i.src2.constant()));
    } else if (i.src2.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
      e.and_(i.dest, i.src1, e.x0);
    } else if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.and_(i.dest, i.src2, e.x0);
    } else {
      e.and_(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_V128 : Sequence<AND_V128, I<OPCODE_AND, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s1 = SrcVReg(e, i.src1, 0);
    int s2 = SrcVReg(e, i.src2, 1);
    e.and_(VReg(i.dest.reg().getIdx()).b16, VReg(s1).b16, VReg(s2).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND, AND_I8, AND_I16, AND_I32, AND_I64, AND_V128);

// ============================================================================
// OPCODE_AND_NOT
// ============================================================================
struct AND_NOT_I8 : Sequence<AND_NOT_I8, I<OPCODE_AND_NOT, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 & ~src2 -> bic dest, src1, src2
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() & ~i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFF));
      e.bic(i.dest, i.src1, e.w0);
    } else if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      e.bic(i.dest, e.w0, i.src2);
    } else {
      e.bic(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_NOT_I16
    : Sequence<AND_NOT_I16, I<OPCODE_AND_NOT, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() & ~i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
      e.bic(i.dest, i.src1, e.w0);
    } else if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
      e.bic(i.dest, e.w0, i.src2);
    } else {
      e.bic(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_NOT_I32
    : Sequence<AND_NOT_I32, I<OPCODE_AND_NOT, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() & ~i.src2.constant())));
    } else if (i.src2.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      e.bic(i.dest, i.src1, e.w0);
    } else if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      e.bic(i.dest, e.w0, i.src2);
    } else {
      e.bic(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_NOT_I64
    : Sequence<AND_NOT_I64, I<OPCODE_AND_NOT, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() & ~i.src2.constant()));
    } else if (i.src2.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
      e.bic(i.dest, i.src1, e.x0);
    } else if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.bic(i.dest, e.x0, i.src2);
    } else {
      e.bic(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_NOT_V128
    : Sequence<AND_NOT_V128, I<OPCODE_AND_NOT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // AND_NOT = src1 AND (NOT src2) = BIC(src1, src2)
    int s1 = SrcVReg(e, i.src1, 0);
    int s2 = SrcVReg(e, i.src2, 1);
    e.bic(VReg(i.dest.reg().getIdx()).b16, VReg(s1).b16, VReg(s2).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND_NOT, AND_NOT_I8, AND_NOT_I16, AND_NOT_I32,
                     AND_NOT_I64, AND_NOT_V128);

// ============================================================================
// OPCODE_OR
// ============================================================================
struct OR_I8 : Sequence<OR_I8, I<OPCODE_OR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() | i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      e.orr_imm(i.dest, i.src1, i.src2.constant() & 0xFF, e.w0);
    } else if (i.src1.is_constant) {
      e.orr_imm(i.dest, i.src2, i.src1.constant() & 0xFF, e.w0);
    } else {
      e.orr(i.dest, i.src1, i.src2);
    }
  }
};
struct OR_I16 : Sequence<OR_I16, I<OPCODE_OR, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() | i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      e.orr_imm(i.dest, i.src1, i.src2.constant() & 0xFFFF, e.w0);
    } else if (i.src1.is_constant) {
      e.orr_imm(i.dest, i.src2, i.src1.constant() & 0xFFFF, e.w0);
    } else {
      e.orr(i.dest, i.src1, i.src2);
    }
  }
};
struct OR_I32 : Sequence<OR_I32, I<OPCODE_OR, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() | i.src2.constant())));
    } else if (i.src2.is_constant) {
      e.orr_imm(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant()), e.w0);
    } else if (i.src1.is_constant) {
      e.orr_imm(i.dest, i.src2, static_cast<uint32_t>(i.src1.constant()), e.w0);
    } else {
      e.orr(i.dest, i.src1, i.src2);
    }
  }
};
struct OR_I64 : Sequence<OR_I64, I<OPCODE_OR, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() | i.src2.constant()));
    } else if (i.src2.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
      e.orr(i.dest, i.src1, e.x0);
    } else if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.orr(i.dest, i.src2, e.x0);
    } else {
      e.orr(i.dest, i.src1, i.src2);
    }
  }
};
struct OR_V128 : Sequence<OR_V128, I<OPCODE_OR, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s1 = SrcVReg(e, i.src1, 0);
    int s2 = SrcVReg(e, i.src2, 1);
    e.orr(VReg(i.dest.reg().getIdx()).b16, VReg(s1).b16, VReg(s2).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_OR, OR_I8, OR_I16, OR_I32, OR_I64, OR_V128);

// ============================================================================
// OPCODE_XOR
// ============================================================================
struct XOR_I8 : Sequence<XOR_I8, I<OPCODE_XOR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() ^ i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      e.eor_imm(i.dest, i.src1, i.src2.constant() & 0xFF, e.w0);
    } else if (i.src1.is_constant) {
      e.eor_imm(i.dest, i.src2, i.src1.constant() & 0xFF, e.w0);
    } else {
      e.eor(i.dest, i.src1, i.src2);
    }
  }
};
struct XOR_I16 : Sequence<XOR_I16, I<OPCODE_XOR, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() ^ i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      e.eor_imm(i.dest, i.src1, i.src2.constant() & 0xFFFF, e.w0);
    } else if (i.src1.is_constant) {
      e.eor_imm(i.dest, i.src2, i.src1.constant() & 0xFFFF, e.w0);
    } else {
      e.eor(i.dest, i.src1, i.src2);
    }
  }
};
struct XOR_I32 : Sequence<XOR_I32, I<OPCODE_XOR, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() ^ i.src2.constant())));
    } else if (i.src2.is_constant) {
      e.eor_imm(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant()), e.w0);
    } else if (i.src1.is_constant) {
      e.eor_imm(i.dest, i.src2, static_cast<uint32_t>(i.src1.constant()), e.w0);
    } else {
      e.eor(i.dest, i.src1, i.src2);
    }
  }
};
struct XOR_I64 : Sequence<XOR_I64, I<OPCODE_XOR, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() ^ i.src2.constant()));
    } else if (i.src2.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
      e.eor(i.dest, i.src1, e.x0);
    } else if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.eor(i.dest, i.src2, e.x0);
    } else {
      e.eor(i.dest, i.src1, i.src2);
    }
  }
};
struct XOR_V128 : Sequence<XOR_V128, I<OPCODE_XOR, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s1 = SrcVReg(e, i.src1, 0);
    int s2 = SrcVReg(e, i.src2, 1);
    e.eor(VReg(i.dest.reg().getIdx()).b16, VReg(s1).b16, VReg(s2).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_XOR, XOR_I8, XOR_I16, XOR_I32, XOR_I64, XOR_V128);

// ============================================================================
// OPCODE_NOT
// ============================================================================
struct NOT_I8 : Sequence<NOT_I8, I<OPCODE_NOT, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint8_t>(~i.src1.constant())));
    } else {
      e.mvn(i.dest, i.src1);
    }
  }
};
struct NOT_I16 : Sequence<NOT_I16, I<OPCODE_NOT, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint16_t>(~i.src1.constant())));
    } else {
      e.mvn(i.dest, i.src1);
    }
  }
};
struct NOT_I32 : Sequence<NOT_I32, I<OPCODE_NOT, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint32_t>(~i.src1.constant())));
    } else {
      e.mvn(i.dest, i.src1);
    }
  }
};
struct NOT_I64 : Sequence<NOT_I64, I<OPCODE_NOT, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(~i.src1.constant()));
    } else {
      e.mvn(i.dest, i.src1);
    }
  }
};
struct NOT_V128 : Sequence<NOT_V128, I<OPCODE_NOT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s = SrcVReg(e, i.src1, 0);
    e.not_(VReg(i.dest.reg().getIdx()).b16, VReg(s).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NOT, NOT_I8, NOT_I16, NOT_I32, NOT_I64, NOT_V128);

// ============================================================================
// OPCODE_SHL
// ============================================================================
struct SHL_I8 : Sequence<SHL_I8, I<OPCODE_SHL, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint8_t>(
                          i.src1.constant() << (i.src2.constant() & 0x7))));
      } else {
        e.lsl(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsl(i.dest, i.dest, e.w0);
    }
  }
};
struct SHL_I16 : Sequence<SHL_I16, I<OPCODE_SHL, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint16_t>(
                          i.src1.constant() << (i.src2.constant() & 0xF))));
      } else {
        e.lsl(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsl(i.dest, i.dest, e.w0);
    }
  }
};
struct SHL_I32 : Sequence<SHL_I32, I<OPCODE_SHL, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                          i.src1.constant() << (i.src2.constant() & 0x1F))));
      } else {
        e.lsl(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsl(i.dest, i.dest, e.w0);
    }
  }
};
struct SHL_I64 : Sequence<SHL_I64, I<OPCODE_SHL, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()
                                            << (i.src2.constant() & 0x3F)));
      } else {
        e.lsl(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x3F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.x0, XReg(i.src2.reg().getIdx()));
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsl(i.dest, i.dest, e.x0);
    }
  }
};
struct SHL_V128 : Sequence<SHL_V128, I<OPCODE_SHL, V128Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // PPC 128-bit SHL by N bits (0-7). The value is stored as 4 word-swapped
    // 32-bit lanes. Carries flow from higher NEON lanes to lower:
    //   lane[i] = (lane[i] << N) | (lane[i+1] >> (32-N))
    int s = SrcVReg(e, i.src1, 0);
    int d = i.dest.reg().getIdx();
    if (i.src2.is_constant) {
      uint8_t sh = i.src2.constant() & 0x7;
      if (sh == 0) {
        if (d != s) e.mov(VReg(d).b16, VReg(s).b16);
        return;
      }
      // Read carry before writing result (handles dest==src aliasing).
      e.ushr(VReg(0).s4, VReg(s).s4, 32 - sh);
      e.shl(VReg(d).s4, VReg(s).s4, sh);
    } else {
      // Variable shift: mask to 0-7, splat, use ushl.
      e.and_(e.w0, WReg(i.src2.reg().getIdx()), 7);
      e.dup(VReg(1).s4, e.w0);
      e.movi(VReg(2).s4, 32);
      e.sub(VReg(2).s4, VReg(2).s4, VReg(1).s4);   // 32-N
      e.neg(VReg(2).s4, VReg(2).s4);               // -(32-N) for right shift
      e.ushl(VReg(0).s4, VReg(s).s4, VReg(2).s4);  // carry: lane >> (32-N)
      e.ushl(VReg(d).s4, VReg(s).s4, VReg(1).s4);  // result: lane << N
    }
    // Shift carries from lane i+1 to lane i; lane 3 gets zero.
    e.movi(VReg(1).s4, 0);
    e.ext(VReg(0).b16, VReg(0).b16, VReg(1).b16, 4);
    e.orr(VReg(d).b16, VReg(d).b16, VReg(0).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHL, SHL_I8, SHL_I16, SHL_I32, SHL_I64, SHL_V128);

// ============================================================================
// OPCODE_SHR (logical shift right)
// ============================================================================
struct SHR_I8 : Sequence<SHR_I8, I<OPCODE_SHR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint8_t>(
                          static_cast<uint8_t>(i.src1.constant()) >>
                          (i.src2.constant() & 0x7))));
      } else {
        e.lsr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsr(i.dest, i.dest, e.w0);
    }
  }
};
struct SHR_I16 : Sequence<SHR_I16, I<OPCODE_SHR, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint16_t>(
                          static_cast<uint16_t>(i.src1.constant()) >>
                          (i.src2.constant() & 0xF))));
      } else {
        e.lsr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsr(i.dest, i.dest, e.w0);
    }
  }
};
struct SHR_I32 : Sequence<SHR_I32, I<OPCODE_SHR, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant()) >>
                                    (i.src2.constant() & 0x1F)));
      } else {
        e.lsr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsr(i.dest, i.dest, e.w0);
    }
  }
};
struct SHR_I64 : Sequence<SHR_I64, I<OPCODE_SHR, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()) >>
                          (i.src2.constant() & 0x3F));
      } else {
        e.lsr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x3F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.x0, XReg(i.src2.reg().getIdx()));
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsr(i.dest, i.dest, e.x0);
    }
  }
};
struct SHR_V128 : Sequence<SHR_V128, I<OPCODE_SHR, V128Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // PPC 128-bit SHR by N bits (0-7). Carries flow from lower NEON lanes
    // to higher:
    //   lane[i] = (lane[i] >> N) | (lane[i-1] << (32-N))
    int s = SrcVReg(e, i.src1, 0);
    int d = i.dest.reg().getIdx();
    if (i.src2.is_constant) {
      uint8_t sh = i.src2.constant() & 0x7;
      if (sh == 0) {
        if (d != s) e.mov(VReg(d).b16, VReg(s).b16);
        return;
      }
      // Read carry before writing result (handles dest==src aliasing).
      e.shl(VReg(0).s4, VReg(s).s4, 32 - sh);
      e.ushr(VReg(d).s4, VReg(s).s4, sh);
    } else {
      // Variable shift: mask to 0-7, splat, use ushl.
      e.and_(e.w0, WReg(i.src2.reg().getIdx()), 7);
      e.dup(VReg(1).s4, e.w0);
      e.movi(VReg(2).s4, 32);
      e.sub(VReg(2).s4, VReg(2).s4, VReg(1).s4);   // 32-N
      e.ushl(VReg(0).s4, VReg(s).s4, VReg(2).s4);  // carry: lane << (32-N)
      e.neg(VReg(1).s4, VReg(1).s4);               // -N for right shift
      e.ushl(VReg(d).s4, VReg(s).s4, VReg(1).s4);  // result: lane >> N
    }
    // Shift carries from lane i-1 to lane i; lane 0 gets zero.
    e.movi(VReg(1).s4, 0);
    e.ext(VReg(0).b16, VReg(1).b16, VReg(0).b16, 12);
    e.orr(VReg(d).b16, VReg(d).b16, VReg(0).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHR, SHR_I8, SHR_I16, SHR_I32, SHR_I64, SHR_V128);

// ============================================================================
// OPCODE_SHA (arithmetic shift right)
// ============================================================================
struct SHA_I8 : Sequence<SHA_I8, I<OPCODE_SHA, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Sign-extend to 32-bit, then ASR.
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    e.sxtb(e.w0, e.w0);
    if (i.src2.is_constant) {
      e.asr(i.dest, e.w0, static_cast<uint32_t>(i.src2.constant() & 0x1F));
    } else {
      e.asr(i.dest, e.w0, i.src2);
    }
  }
};
struct SHA_I16 : Sequence<SHA_I16, I<OPCODE_SHA, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    e.sxth(e.w0, e.w0);
    if (i.src2.is_constant) {
      e.asr(i.dest, e.w0, static_cast<uint32_t>(i.src2.constant() & 0x1F));
    } else {
      e.asr(i.dest, e.w0, i.src2);
    }
  }
};
struct SHA_I32 : Sequence<SHA_I32, I<OPCODE_SHA, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                          i.src1.constant() >> (i.src2.constant() & 0x1F))));
      } else {
        e.asr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.asr(i.dest, i.dest, e.w0);
    }
  }
};
struct SHA_I64 : Sequence<SHA_I64, I<OPCODE_SHA, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() >>
                                            (i.src2.constant() & 0x3F)));
      } else {
        e.asr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x3F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.x0, XReg(i.src2.reg().getIdx()));
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.asr(i.dest, i.dest, e.x0);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHA, SHA_I8, SHA_I16, SHA_I32, SHA_I64);

// ============================================================================
// OPCODE_ROTATE_LEFT
// ============================================================================
struct ROTATE_LEFT_I8
    : Sequence<ROTATE_LEFT_I8, I<OPCODE_ROTATE_LEFT, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // ARM64 has ROR but no ROL. ROL(x, n) = ROR(x, size - n).
    // For 8-bit: duplicate into both halves of a 16-bit val, then shift.
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    } else {
      e.uxtb(e.w0, i.src1);
    }
    // Duplicate byte into bits [15:8] too: w0 = (w0 | (w0 << 8))
    e.orr(e.w0, e.w0, e.w0, Xbyak_aarch64::LSL, 8);
    if (i.src2.is_constant) {
      uint32_t amt = i.src2.constant() & 0x7;
      if (amt) {
        e.lsr(e.w0, e.w0, static_cast<uint32_t>(8 - amt));
      }
    } else {
      // shift = 8 - (src2 & 7)
      e.mov(e.w1, 8);
      e.and_(e.w2, i.src2, 7);
      e.sub(e.w1, e.w1, e.w2);
      e.lsr(e.w0, e.w0, e.w1);
    }
    e.uxtb(i.dest, e.w0);
  }
};
struct ROTATE_LEFT_I16
    : Sequence<ROTATE_LEFT_I16, I<OPCODE_ROTATE_LEFT, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.uxth(e.w0, i.src1);
    }
    e.orr(e.w0, e.w0, e.w0, Xbyak_aarch64::LSL, 16);
    if (i.src2.is_constant) {
      uint32_t amt = i.src2.constant() & 0xF;
      if (amt) {
        e.lsr(e.w0, e.w0, static_cast<uint32_t>(16 - amt));
      }
    } else {
      e.mov(e.w1, 16);
      e.and_(e.w2, i.src2, 0xF);
      e.sub(e.w1, e.w1, e.w2);
      e.lsr(e.w0, e.w0, e.w1);
    }
    e.uxth(i.dest, e.w0);
  }
};
struct ROTATE_LEFT_I32
    : Sequence<ROTATE_LEFT_I32, I<OPCODE_ROTATE_LEFT, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // ROL(x, n) = ROR(x, 32 - n)
    if (i.src2.is_constant) {
      uint32_t amt = i.src2.constant() & 0x1F;
      if (amt == 0) {
        if (i.src1.is_constant) {
          e.mov(i.dest, static_cast<uint64_t>(
                            static_cast<uint32_t>(i.src1.constant())));
        } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
          e.mov(i.dest, i.src1);
        }
      } else {
        if (i.src1.is_constant) {
          e.mov(e.w0, static_cast<uint64_t>(
                          static_cast<uint32_t>(i.src1.constant())));
          e.ror(i.dest, e.w0, static_cast<uint32_t>(32 - amt));
        } else {
          e.ror(i.dest, i.src1, static_cast<uint32_t>(32 - amt));
        }
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      // ROL(x, n) = ROR(x, -n) since ROR uses amount mod 32
      e.neg(e.w0, e.w0);
      e.ror(i.dest, i.dest, e.w0);
    }
  }
};
struct ROTATE_LEFT_I64
    : Sequence<ROTATE_LEFT_I64, I<OPCODE_ROTATE_LEFT, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      uint32_t amt = i.src2.constant() & 0x3F;
      if (amt == 0) {
        if (i.src1.is_constant) {
          e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
        } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
          e.mov(i.dest, i.src1);
        }
      } else {
        if (i.src1.is_constant) {
          e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
          e.ror(i.dest, e.x0, static_cast<uint32_t>(64 - amt));
        } else {
          e.ror(i.dest, i.src1, static_cast<uint32_t>(64 - amt));
        }
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.x0, XReg(i.src2.reg().getIdx()));
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      // ROL(x, n) = ROR(x, -n) since ROR uses amount mod 64
      e.neg(e.x0, e.x0);
      e.ror(i.dest, i.dest, e.x0);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROTATE_LEFT, ROTATE_LEFT_I8, ROTATE_LEFT_I16,
                     ROTATE_LEFT_I32, ROTATE_LEFT_I64);

// ============================================================================
// OPCODE_BYTE_SWAP
// ============================================================================
struct BYTE_SWAP_I16
    : Sequence<BYTE_SWAP_I16, I<OPCODE_BYTE_SWAP, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint16_t v = i.src1.constant();
      v = (v >> 8) | (v << 8);
      e.mov(i.dest, static_cast<uint64_t>(v));
    } else {
      e.rev16(i.dest, i.src1);
    }
  }
};
struct BYTE_SWAP_I32
    : Sequence<BYTE_SWAP_I32, I<OPCODE_BYTE_SWAP, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(xe::byte_swap(
                        static_cast<uint32_t>(i.src1.constant()))));
    } else {
      e.rev(i.dest, i.src1);
    }
  }
};
struct BYTE_SWAP_I64
    : Sequence<BYTE_SWAP_I64, I<OPCODE_BYTE_SWAP, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, xe::byte_swap(static_cast<uint64_t>(i.src1.constant())));
    } else {
      e.rev(i.dest, i.src1);
    }
  }
};
struct BYTE_SWAP_V128
    : Sequence<BYTE_SWAP_V128, I<OPCODE_BYTE_SWAP, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s = SrcVReg(e, i.src1, 0);
    e.rev32(VReg(i.dest.reg().getIdx()).b16, VReg(s).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BYTE_SWAP, BYTE_SWAP_I16, BYTE_SWAP_I32,
                     BYTE_SWAP_I64, BYTE_SWAP_V128);

// ============================================================================
// OPCODE_CNTLZ
// ============================================================================
struct CNTLZ_I8 : Sequence<CNTLZ_I8, I<OPCODE_CNTLZ, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint8_t v = static_cast<uint8_t>(i.src1.constant());
      uint8_t count = 0;
      while (count < 8 && !(v & 0x80)) {
        v <<= 1;
        count++;
      }
      e.mov(i.dest, static_cast<uint64_t>(count));
    } else {
      // clz operates on 32-bit, so shift left 24 to put byte in top.
      // OR a sentinel bit at position 23 so that a zero byte yields 8,
      // not 32.
      e.lsl(e.w0, i.src1, 24);
      e.orr(e.w0, e.w0, 1u << 23);
      e.clz(i.dest, e.w0);
    }
  }
};
struct CNTLZ_I16 : Sequence<CNTLZ_I16, I<OPCODE_CNTLZ, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint16_t v = static_cast<uint16_t>(i.src1.constant());
      uint8_t count = 0;
      while (count < 16 && !(v & 0x8000)) {
        v <<= 1;
        count++;
      }
      e.mov(i.dest, static_cast<uint64_t>(count));
    } else {
      // Sentinel bit at position 15 caps the result at 16 for zero input.
      e.lsl(e.w0, i.src1, 16);
      e.orr(e.w0, e.w0, 1u << 15);
      e.clz(i.dest, e.w0);
    }
  }
};
struct CNTLZ_I32 : Sequence<CNTLZ_I32, I<OPCODE_CNTLZ, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint32_t v = static_cast<uint32_t>(i.src1.constant());
      e.mov(i.dest, static_cast<uint64_t>(xe::lzcnt(v)));
    } else {
      e.clz(i.dest, i.src1);
    }
  }
};
struct CNTLZ_I64 : Sequence<CNTLZ_I64, I<OPCODE_CNTLZ, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint64_t v = static_cast<uint64_t>(i.src1.constant());
      e.mov(i.dest, static_cast<uint64_t>(xe::lzcnt(v)));
    } else {
      // clz on XReg returns into XReg, we need WReg dest.
      e.clz(e.x0, i.src1);
      e.mov(i.dest, e.w0);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CNTLZ, CNTLZ_I8, CNTLZ_I16, CNTLZ_I32, CNTLZ_I64);

// ============================================================================
// Compare helpers
// ============================================================================
// ARM64: cmp src1, src2; cset dest, <cond>
// For I8/I16/I32 the dest is I8Op (WReg).
// For constants, load into scratch first.

#define DEFINE_COMPARE_XX(NAME, COND)                                          \
  struct NAME##_I8 : Sequence<NAME##_I8, I<OPCODE_##NAME, I8Op, I8Op, I8Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));          \
        if (i.src2.is_constant) {                                              \
          e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFF));        \
          e.cmp(e.w0, e.w1);                                                   \
        } else {                                                               \
          e.cmp(e.w0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint32_t imm = static_cast<uint32_t>(i.src2.constant() & 0xFF);        \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, imm);                                                  \
        } else {                                                               \
          e.mov(e.w0, static_cast<uint64_t>(imm));                             \
          e.cmp(i.src1, e.w0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I16                                                            \
      : Sequence<NAME##_I16, I<OPCODE_##NAME, I8Op, I16Op, I16Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));        \
        if (i.src2.is_constant) {                                              \
          e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));      \
          e.cmp(e.w0, e.w1);                                                   \
        } else {                                                               \
          e.cmp(e.w0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint32_t imm = static_cast<uint32_t>(i.src2.constant() & 0xFFFF);      \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, imm);                                                  \
        } else {                                                               \
          e.mov(e.w0, static_cast<uint64_t>(imm));                             \
          e.cmp(i.src1, e.w0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I32                                                            \
      : Sequence<NAME##_I32, I<OPCODE_##NAME, I8Op, I32Op, I32Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(                                     \
                        static_cast<uint32_t>(i.src1.constant())));            \
        if (i.src2.is_constant) {                                              \
          e.mov(e.w1, static_cast<uint64_t>(                                   \
                          static_cast<uint32_t>(i.src2.constant())));          \
          e.cmp(e.w0, e.w1);                                                   \
        } else {                                                               \
          e.cmp(e.w0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint32_t imm = static_cast<uint32_t>(i.src2.constant());               \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, imm);                                                  \
        } else {                                                               \
          e.mov(e.w0, static_cast<uint64_t>(imm));                             \
          e.cmp(i.src1, e.w0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I64                                                            \
      : Sequence<NAME##_I64, I<OPCODE_##NAME, I8Op, I64Op, I64Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));                 \
        if (i.src2.is_constant) {                                              \
          e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));               \
          e.cmp(e.x0, e.x1);                                                   \
        } else {                                                               \
          e.cmp(e.x0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint64_t imm = static_cast<uint64_t>(i.src2.constant());               \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, static_cast<uint32_t>(imm));                           \
        } else {                                                               \
          e.mov(e.x0, imm);                                                    \
          e.cmp(i.src1, e.x0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct _tag_##NAME {}

DEFINE_COMPARE_XX(COMPARE_EQ, EQ);
DEFINE_COMPARE_XX(COMPARE_NE, NE);
// Signed I8/I16 comparisons need sign-extension to 32-bit because ARM64
// cmp always operates on full 32-bit WRegs. Without sign-extension,
// 0xFF (which is -1 as signed I8) would compare as 255, giving wrong results.
#define DEFINE_SIGNED_COMPARE_XX(NAME, COND)                                   \
  struct NAME##_I8 : Sequence<NAME##_I8, I<OPCODE_##NAME, I8Op, I8Op, I8Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));          \
      } else {                                                                 \
        e.mov(e.w0, i.src1);                                                   \
      }                                                                        \
      e.sxtb(e.w0, e.w0);                                                      \
      if (i.src2.is_constant) {                                                \
        e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFF));          \
        e.sxtb(e.w1, e.w1);                                                    \
        e.cmp(e.w0, e.w1);                                                     \
      } else {                                                                 \
        e.sxtb(e.w1, i.src2);                                                  \
        e.cmp(e.w0, e.w1);                                                     \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I16                                                            \
      : Sequence<NAME##_I16, I<OPCODE_##NAME, I8Op, I16Op, I16Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));        \
      } else {                                                                 \
        e.mov(e.w0, i.src1);                                                   \
      }                                                                        \
      e.sxth(e.w0, e.w0);                                                      \
      if (i.src2.is_constant) {                                                \
        e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));        \
        e.sxth(e.w1, e.w1);                                                    \
        e.cmp(e.w0, e.w1);                                                     \
      } else {                                                                 \
        e.sxth(e.w1, i.src2);                                                  \
        e.cmp(e.w0, e.w1);                                                     \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I32                                                            \
      : Sequence<NAME##_I32, I<OPCODE_##NAME, I8Op, I32Op, I32Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(                                     \
                        static_cast<uint32_t>(i.src1.constant())));            \
        if (i.src2.is_constant) {                                              \
          e.mov(e.w1, static_cast<uint64_t>(                                   \
                          static_cast<uint32_t>(i.src2.constant())));          \
          e.cmp(e.w0, e.w1);                                                   \
        } else {                                                               \
          e.cmp(e.w0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint32_t imm = static_cast<uint32_t>(i.src2.constant());               \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, imm);                                                  \
        } else {                                                               \
          e.mov(e.w0, static_cast<uint64_t>(imm));                             \
          e.cmp(i.src1, e.w0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I64                                                            \
      : Sequence<NAME##_I64, I<OPCODE_##NAME, I8Op, I64Op, I64Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));                 \
        if (i.src2.is_constant) {                                              \
          e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));               \
          e.cmp(e.x0, e.x1);                                                   \
        } else {                                                               \
          e.cmp(e.x0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint64_t imm = static_cast<uint64_t>(i.src2.constant());               \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, static_cast<uint32_t>(imm));                           \
        } else {                                                               \
          e.mov(e.x0, imm);                                                    \
          e.cmp(i.src1, e.x0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct _tag_##NAME {}

DEFINE_SIGNED_COMPARE_XX(COMPARE_SLT, LT);
DEFINE_SIGNED_COMPARE_XX(COMPARE_SLE, LE);
DEFINE_SIGNED_COMPARE_XX(COMPARE_SGT, GT);
DEFINE_SIGNED_COMPARE_XX(COMPARE_SGE, GE);
DEFINE_COMPARE_XX(COMPARE_ULT, LO);
DEFINE_COMPARE_XX(COMPARE_ULE, LS);
DEFINE_COMPARE_XX(COMPARE_UGT, HI);
DEFINE_COMPARE_XX(COMPARE_UGE, HS);

#undef DEFINE_COMPARE_XX

// Integer-only compare registrations are deferred until after float
// compare definitions below.

// ============================================================================
// OPCODE_SELECT
// ============================================================================
// dest = src1 ? src2 : src3
struct SELECT_I8
    : Sequence<SELECT_I8, I<OPCODE_SELECT, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    if (i.src2.is_constant) {
      e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFF));
    }
    if (i.src3.is_constant) {
      e.mov(e.w2, static_cast<uint64_t>(i.src3.constant() & 0xFF));
    }
    WReg s2 = i.src2.is_constant ? e.w1 : WReg(i.src2.reg().getIdx());
    WReg s3 = i.src3.is_constant ? e.w2 : WReg(i.src3.reg().getIdx());
    e.csel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_I16
    : Sequence<SELECT_I16, I<OPCODE_SELECT, I16Op, I8Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    if (i.src2.is_constant) {
      e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
    }
    if (i.src3.is_constant) {
      e.mov(e.w2, static_cast<uint64_t>(i.src3.constant() & 0xFFFF));
    }
    WReg s2 = i.src2.is_constant ? e.w1 : WReg(i.src2.reg().getIdx());
    WReg s3 = i.src3.is_constant ? e.w2 : WReg(i.src3.reg().getIdx());
    e.csel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_I32
    : Sequence<SELECT_I32, I<OPCODE_SELECT, I32Op, I8Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    if (i.src2.is_constant) {
      e.mov(e.w1,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    }
    if (i.src3.is_constant) {
      e.mov(e.w2,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src3.constant())));
    }
    WReg s2 = i.src2.is_constant ? e.w1 : WReg(i.src2.reg().getIdx());
    WReg s3 = i.src3.is_constant ? e.w2 : WReg(i.src3.reg().getIdx());
    e.csel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_I64
    : Sequence<SELECT_I64, I<OPCODE_SELECT, I64Op, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    if (i.src2.is_constant) {
      e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));
    }
    if (i.src3.is_constant) {
      e.mov(e.x2, static_cast<uint64_t>(i.src3.constant()));
    }
    XReg s2 = i.src2.is_constant ? e.x1 : XReg(i.src2.reg().getIdx());
    XReg s3 = i.src3.is_constant ? e.x2 : XReg(i.src3.reg().getIdx());
    e.csel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_F32
    : Sequence<SELECT_F32, I<OPCODE_SELECT, F32Op, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w1, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w1);
    }
    if (i.src3.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src3.constant();
      e.mov(e.w1, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w1);
    }
    SReg s2 = i.src2.is_constant ? e.s0 : SReg(i.src2.reg().getIdx());
    SReg s3 = i.src3.is_constant ? e.s1 : SReg(i.src3.reg().getIdx());
    e.fcsel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_F64
    : Sequence<SELECT_F64, I<OPCODE_SELECT, F64Op, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x1, c.u);
      e.fmov(e.d0, e.x1);
    }
    if (i.src3.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src3.constant();
      e.mov(e.x1, c.u);
      e.fmov(e.d1, e.x1);
    }
    DReg s2 = i.src2.is_constant ? e.d0 : DReg(i.src2.reg().getIdx());
    DReg s3 = i.src3.is_constant ? e.d1 : DReg(i.src3.reg().getIdx());
    e.fcsel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_V128_V128
    : Sequence<SELECT_V128_V128,
               I<OPCODE_SELECT, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int d = i.dest.reg().getIdx();
    int s1 = SrcVReg(e, i.src1, 0);  // condition mask
    int s2 = SrcVReg(e, i.src2, 1);  // value for mask=0
    int s3 = SrcVReg(e, i.src3, 2);  // value for mask=1
    // PPC vsel / HIR SELECT V128: bit=1 → src3, bit=0 → src2
    // ARM64 BIT: dest = (op1 & mask) | (dest & ~mask) — keeps dest where mask=0
    // ARM64 BIF: dest = (dest & mask) | (op1 & ~mask) — keeps dest where mask=1
    // Use BIT/BIF to avoid clobbering when dest aliases an operand.
    if (d == s1) {
      // dest already holds the mask. BSL is safe here.
      e.bsl(VReg(d).b16, VReg(s3).b16, VReg(s2).b16);
    } else if (d == s3) {
      // dest holds the mask=1 value. BIT inserts mask=1 bits from s3, keeps
      // dest (=s2-candidate) where mask=0... no, dest=s3 not s2.
      // Use: copy s2 to scratch, then BIT(scratch, s3, mask), move to dest.
      // Or: copy mask to scratch v0, copy s2 to dest, BIT(dest, s3_orig, v0).
      // Simplest: use scratch v0 for mask, then BSL.
      e.orr(VReg(0).b16, VReg(s1).b16, VReg(s1).b16);  // v0 = mask
      e.bsl(VReg(0).b16, VReg(s3).b16, VReg(s2).b16);  // v0 = result
      e.orr(VReg(d).b16, VReg(0).b16, VReg(0).b16);    // dest = result
    } else if (d == s2) {
      // dest holds the mask=0 value. BIF inserts ~mask bits from s2,
      // but dest=s2... Use scratch for mask.
      e.orr(VReg(0).b16, VReg(s1).b16, VReg(s1).b16);  // v0 = mask
      e.bsl(VReg(0).b16, VReg(s3).b16, VReg(s2).b16);  // v0 = result
      e.orr(VReg(d).b16, VReg(0).b16, VReg(0).b16);    // dest = result
    } else {
      // No aliasing — copy mask to dest, then BSL.
      e.orr(VReg(d).b16, VReg(s1).b16, VReg(s1).b16);
      e.bsl(VReg(d).b16, VReg(s3).b16, VReg(s2).b16);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SELECT, SELECT_I8, SELECT_I16, SELECT_I32,
                     SELECT_I64, SELECT_F32, SELECT_F64, SELECT_V128_V128);

// ============================================================================
// OPCODE_LOAD_LOCAL
// ============================================================================
// Note: all types are always aligned on the stack.
// For large offsets that don't fit in the unsigned immediate field of
// LDR/STR, compute the effective address in a temp register first.
static inline bool LocalOffsetFitsImm(uint32_t offset, uint32_t scale) {
  return (offset % scale) == 0 && (offset / scale) <= 0xFFF;
}
// Compute base register for local access; returns {base, imm} pair.
// If the offset fits the scaled immediate, returns {sp, offset}.
// Otherwise loads sp+offset into x17 and returns {x17, 0}.
static inline XReg PrepareLocalBase(A64Emitter& e, uint32_t offset,
                                    uint32_t scale) {
  if (LocalOffsetFitsImm(offset, scale)) {
    return e.sp;
  }
  e.mov(e.x17, static_cast<uint64_t>(offset));
  e.add(e.x17, e.sp, e.x17);
  return e.x17;
}
static inline uint32_t PrepareLocalImm(uint32_t offset, uint32_t scale) {
  if (LocalOffsetFitsImm(offset, scale)) {
    return offset;
  }
  return 0;
}

struct LOAD_LOCAL_I8
    : Sequence<LOAD_LOCAL_I8, I<OPCODE_LOAD_LOCAL, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 1);
    e.ldrb(i.dest, ptr(base, PrepareLocalImm(off, 1)));
  }
};
struct LOAD_LOCAL_I16
    : Sequence<LOAD_LOCAL_I16, I<OPCODE_LOAD_LOCAL, I16Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 2);
    e.ldrh(i.dest, ptr(base, PrepareLocalImm(off, 2)));
  }
};
struct LOAD_LOCAL_I32
    : Sequence<LOAD_LOCAL_I32, I<OPCODE_LOAD_LOCAL, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 4);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 4)));
  }
};
struct LOAD_LOCAL_I64
    : Sequence<LOAD_LOCAL_I64, I<OPCODE_LOAD_LOCAL, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 8);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 8)));
  }
};
struct LOAD_LOCAL_F32
    : Sequence<LOAD_LOCAL_F32, I<OPCODE_LOAD_LOCAL, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 4);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 4)));
  }
};
struct LOAD_LOCAL_F64
    : Sequence<LOAD_LOCAL_F64, I<OPCODE_LOAD_LOCAL, F64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 8);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 8)));
  }
};
struct LOAD_LOCAL_V128
    : Sequence<LOAD_LOCAL_V128, I<OPCODE_LOAD_LOCAL, V128Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 16);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 16)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_LOCAL, LOAD_LOCAL_I8, LOAD_LOCAL_I16,
                     LOAD_LOCAL_I32, LOAD_LOCAL_I64, LOAD_LOCAL_F32,
                     LOAD_LOCAL_F64, LOAD_LOCAL_V128);

// ============================================================================
// OPCODE_STORE_LOCAL
// ============================================================================
struct STORE_LOCAL_I8
    : Sequence<STORE_LOCAL_I8, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 1);
    uint32_t imm = PrepareLocalImm(off, 1);
    if (i.src2.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFF));
      e.strb(e.w0, ptr(base, imm));
    } else {
      e.strb(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_I16
    : Sequence<STORE_LOCAL_I16, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 2);
    uint32_t imm = PrepareLocalImm(off, 2);
    if (i.src2.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
      e.strh(e.w0, ptr(base, imm));
    } else {
      e.strh(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_I32
    : Sequence<STORE_LOCAL_I32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 4);
    uint32_t imm = PrepareLocalImm(off, 4);
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0) {
        e.str(e.wzr, ptr(base, imm));
      } else {
        e.mov(e.w0,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
        e.str(e.w0, ptr(base, imm));
      }
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_I64
    : Sequence<STORE_LOCAL_I64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 8);
    uint32_t imm = PrepareLocalImm(off, 8);
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0) {
        e.str(e.xzr, ptr(base, imm));
      } else {
        e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
        e.str(e.x0, ptr(base, imm));
      }
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_F32
    : Sequence<STORE_LOCAL_F32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 4);
    uint32_t imm = PrepareLocalImm(off, 4);
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.str(e.w0, ptr(base, imm));
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_F64
    : Sequence<STORE_LOCAL_F64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 8);
    uint32_t imm = PrepareLocalImm(off, 8);
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.str(e.x0, ptr(base, imm));
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_V128
    : Sequence<STORE_LOCAL_V128, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 16);
    uint32_t imm = PrepareLocalImm(off, 16);
    if (i.src2.is_constant) {
      LoadV128Const(e, 0, i.src2.constant());
      e.str(QReg(0), ptr(base, imm));
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_LOCAL, STORE_LOCAL_I8, STORE_LOCAL_I16,
                     STORE_LOCAL_I32, STORE_LOCAL_I64, STORE_LOCAL_F32,
                     STORE_LOCAL_F64, STORE_LOCAL_V128);

// ============================================================================
// OPCODE_CAST
// ============================================================================
struct CAST_I32_F32 : Sequence<CAST_I32_F32, I<OPCODE_CAST, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Bitcast float -> int (not conversion).
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(i.dest, static_cast<uint64_t>(c.u));
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct CAST_I64_F64 : Sequence<CAST_I64_F64, I<OPCODE_CAST, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(i.dest, c.u);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct CAST_F32_I32 : Sequence<CAST_F32_I32, I<OPCODE_CAST, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      e.fmov(i.dest, e.w0);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct CAST_F64_I64 : Sequence<CAST_F64_I64, I<OPCODE_CAST, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.fmov(i.dest, e.x0);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CAST, CAST_I32_F32, CAST_I64_F64, CAST_F32_I32,
                     CAST_F64_I64);

// ============================================================================
// OPCODE_DID_SATURATE
// ============================================================================
struct DID_SATURATE
    : Sequence<DID_SATURATE, I<OPCODE_DID_SATURATE, I8Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // TODO(has207): Implement saturation tracking. ARM64 NEON saturating
    // ops (sqadd/uqadd/etc.) set FPSR.QC — clear it before the saturating
    // op, then read it here with mrs. Requires coordinating with all
    // ARITHMETIC_SATURATE vector paths. Always returns 0 for now (same as
    // x64 backend).
    e.mov(i.dest, 0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DID_SATURATE, DID_SATURATE);

// ============================================================================
// OPCODE_MAX / OPCODE_MIN (scalar)
// ============================================================================
struct MAX_F32 : Sequence<MAX_F32, I<OPCODE_MAX, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fmax(i.dest, e.s0, i.src2.is_constant ? e.s1 : i.src2);
    } else if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fmax(i.dest, i.src1, e.s0);
    } else {
      e.fmax(i.dest, i.src1, i.src2);
    }
  }
};
struct MAX_F64 : Sequence<MAX_F64, I<OPCODE_MAX, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fmax(i.dest, e.d0, i.src2.is_constant ? e.d1 : i.src2);
    } else if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fmax(i.dest, i.src1, e.d0);
    } else {
      e.fmax(i.dest, i.src1, i.src2);
    }
  }
};
struct MAX_V128 : Sequence<MAX_V128, I<OPCODE_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s1, s2;
      PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
      e.fmax(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
      // PPC vmaxfp: if either input is NaN, result = src1 (vA).
      FixupVmxMaxMinNan(e);
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 2, 0, 1);
      }
      e.mov(VReg(i.dest.reg().getIdx()).b16, VReg(2).b16);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MAX, MAX_F32, MAX_F64, MAX_V128);

// MIN has signed semantics (HIR builder constant-folds using CompareSLT).
// I8/I16 need sign-extension; all need signed condition code (LT not LO).
struct MIN_I8 : Sequence<MIN_I8, I<OPCODE_MIN, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    e.sxtb(e.w0, e.w0);
    if (i.src2.is_constant) {
      e.mov(e.w17, static_cast<uint64_t>(i.src2.constant() & 0xFF));
      e.sxtb(e.w17, e.w17);
    } else {
      e.sxtb(e.w17, i.src2);
    }
    e.cmp(e.w0, e.w17);
    e.csel(i.dest, e.w0, e.w17, LT);
  }
};
struct MIN_I16 : Sequence<MIN_I16, I<OPCODE_MIN, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    e.sxth(e.w0, e.w0);
    if (i.src2.is_constant) {
      e.mov(e.w17, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
      e.sxth(e.w17, e.w17);
    } else {
      e.sxth(e.w17, i.src2);
    }
    e.cmp(e.w0, e.w17);
    e.csel(i.dest, e.w0, e.w17, LT);
  }
};
struct MIN_I32 : Sequence<MIN_I32, I<OPCODE_MIN, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    } else {
      e.mov(e.w0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.w17,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    } else {
      e.mov(e.w17, i.src2);
    }
    e.cmp(e.w0, e.w17);
    e.csel(i.dest, e.w0, e.w17, LT);
  }
};
struct MIN_I64 : Sequence<MIN_I64, I<OPCODE_MIN, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
    } else {
      e.mov(e.x0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.x17, static_cast<uint64_t>(i.src2.constant()));
    } else {
      e.mov(e.x17, i.src2);
    }
    e.cmp(e.x0, e.x17);
    e.csel(i.dest, e.x0, e.x17, LT);
  }
};
struct MIN_F32 : Sequence<MIN_F32, I<OPCODE_MIN, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fmin(i.dest, e.s0, i.src2.is_constant ? e.s1 : i.src2);
    } else if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fmin(i.dest, i.src1, e.s0);
    } else {
      e.fmin(i.dest, i.src1, i.src2);
    }
  }
};
struct MIN_F64 : Sequence<MIN_F64, I<OPCODE_MIN, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fmin(i.dest, e.d0, i.src2.is_constant ? e.d1 : i.src2);
    } else if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fmin(i.dest, i.src1, e.d0);
    } else {
      e.fmin(i.dest, i.src1, i.src2);
    }
  }
};
struct MIN_V128 : Sequence<MIN_V128, I<OPCODE_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s1, s2;
      PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
      e.fmin(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
      // PPC vminfp: if either input is NaN, result = src1 (vA).
      FixupVmxMaxMinNan(e);
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 2, 0, 1);
      }
      e.mov(VReg(i.dest.reg().getIdx()).b16, VReg(2).b16);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MIN, MIN_I8, MIN_I16, MIN_I32, MIN_I64, MIN_F32,
                     MIN_F64, MIN_V128);

// ============================================================================
// OPCODE_CONVERT
// ============================================================================
struct CONVERT_I32_F32
    : Sequence<CONVERT_I32_F32, I<OPCODE_CONVERT, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    SReg src = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.fcvtzs(i.dest, src);
    } else {
      e.frintx(e.s0, src);
      e.fcvtzs(i.dest, e.s0);
    }
  }
};
struct CONVERT_I32_F64
    : Sequence<CONVERT_I32_F64, I<OPCODE_CONVERT, I32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.fcvtzs(i.dest, src);
    } else {
      // Use current FPCR rounding mode: round first, then truncate.
      e.frintx(e.d0, src);
      e.fcvtzs(i.dest, e.d0);
    }
  }
};
struct CONVERT_I64_F64
    : Sequence<CONVERT_I64_F64, I<OPCODE_CONVERT, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.fcvtzs(i.dest, src);
    } else {
      e.frintx(e.d0, src);
      e.fcvtzs(i.dest, e.d0);
    }
  }
};
struct CONVERT_F32_I32
    : Sequence<CONVERT_F32_I32, I<OPCODE_CONVERT, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      e.scvtf(i.dest, e.w0);
    } else {
      e.scvtf(i.dest, i.src1);
    }
  }
};
struct CONVERT_F64_I64
    : Sequence<CONVERT_F64_I64, I<OPCODE_CONVERT, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.scvtf(i.dest, e.x0);
    } else {
      e.scvtf(i.dest, i.src1);
    }
  }
};
struct CONVERT_F32_F64
    : Sequence<CONVERT_F32_F64, I<OPCODE_CONVERT, F32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fcvt(i.dest, e.d0);
    } else {
      e.fcvt(i.dest, i.src1);
    }
  }
};
struct CONVERT_F64_F32
    : Sequence<CONVERT_F64_F32, I<OPCODE_CONVERT, F64Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fcvt(i.dest, e.s0);
    } else {
      e.fcvt(i.dest, i.src1);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CONVERT, CONVERT_I32_F32, CONVERT_I32_F64,
                     CONVERT_I64_F64, CONVERT_F32_I32, CONVERT_F64_I64,
                     CONVERT_F32_F64, CONVERT_F64_F32);

// ============================================================================
// OPCODE_ROUND
// ============================================================================
struct ROUND_F32 : Sequence<ROUND_F32, I<OPCODE_ROUND, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Round mode is in i.instr->flags.
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    auto src = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.frintz(i.dest, src);
        break;
      case ROUND_TO_NEAREST:
        e.frintn(i.dest, src);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.frintm(i.dest, src);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.frintp(i.dest, src);
        break;
      default:
        // ROUND_DYNAMIC - use current rounding mode.
        e.frinti(i.dest, src);
        break;
    }
  }
};
struct ROUND_F64 : Sequence<ROUND_F64, I<OPCODE_ROUND, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    auto src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.frintz(i.dest, src);
        break;
      case ROUND_TO_NEAREST:
        e.frintn(i.dest, src);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.frintm(i.dest, src);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.frintp(i.dest, src);
        break;
      default:
        e.frinti(i.dest, src);
        break;
    }
  }
};
struct ROUND_V128 : Sequence<ROUND_V128, I<OPCODE_ROUND, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s = SrcVReg(e, i.src1, 0);
      auto src = VReg(s).s4;
      auto dst = VReg(i.dest.reg().getIdx()).s4;
      switch (i.instr->flags) {
        case ROUND_TO_ZERO:
          e.frintz(dst, src);
          break;
        case ROUND_TO_NEAREST:
          e.frintn(dst, src);
          break;
        case ROUND_TO_MINUS_INFINITY:
          e.frintm(dst, src);
          break;
        case ROUND_TO_POSITIVE_INFINITY:
          e.frintp(dst, src);
          break;
        default:
          // ROUND_DYNAMIC - use current rounding mode.
          e.frinti(dst, src);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROUND, ROUND_F32, ROUND_F64, ROUND_V128);

// ============================================================================
// OPCODE_SQRT
// ============================================================================
struct SQRT_F32 : Sequence<SQRT_F32, I<OPCODE_SQRT, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fsqrt(i.dest, e.s0);
    } else {
      e.fsqrt(i.dest, i.src1);
    }
  }
};
struct SQRT_F64 : Sequence<SQRT_F64, I<OPCODE_SQRT, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fsqrt(i.dest, e.d0);
    } else {
      e.fsqrt(i.dest, i.src1);
    }
  }
};
struct SQRT_V128 : Sequence<SQRT_V128, I<OPCODE_SQRT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      if (i.src1.is_constant) {
        e.LoadConstantV(Q0, i.src1.constant());
        e.FSQRT(i.dest.reg().S4(), Q0.S4());
      } else {
        e.FSQRT(i.dest.reg().S4(), i.src1.reg().S4());
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SQRT, SQRT_F32, SQRT_F64, SQRT_V128);

// ============================================================================
// OPCODE_IS_NAN
// ============================================================================
inline uint32_t CountLeadingZeros32(uint32_t value) {
  return value ? static_cast<uint32_t>(__builtin_clz(value)) : 32u;
}

inline uint32_t CountLeadingZeros64(uint64_t value) {
  return value ? static_cast<uint32_t>(__builtin_clzll(value)) : 64u;
}

inline uint32_t FloatToBits(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline float BitsToFloat(uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

inline uint64_t DoubleToBits(double value) {
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline double BitsToDouble(uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

// Copied from x64 backend to match PPC estimate behavior.
static constexpr uint32_t kVrsqrteTable[32] = {
    0x0568B4FDu, 0x04F3AF97u, 0x048DAAA5u, 0x0435A618u, 0x03E7A1E4u,
    0x03A29DFEu, 0x03659A5Cu, 0x032E96F8u, 0x02FC93CAu, 0x02D090CEu,
    0x02A88DFEu, 0x02838B57u, 0x026188D4u, 0x02438673u, 0x02268431u,
    0x020B820Bu, 0x03D27FFAu, 0x03807C29u, 0x033878AAu, 0x02F97572u,
    0x02C27279u, 0x02926FB7u, 0x02666D26u, 0x023F6AC0u, 0x021D6881u,
    0x01FD6665u, 0x01E16468u, 0x01C76287u, 0x01AF60C1u, 0x01995F12u,
    0x01855D79u, 0x01735BF4u,
};

static constexpr uint8_t kFrsqrteTable[16] = {
    241u, 216u, 192u, 168u, 152u, 136u, 128u, 112u,
    96u,  76u,  60u,  48u,  32u,  24u,  16u,  8u,
};

inline uint32_t EmulateVrsqrteScalarBits(uint32_t src_bits,
                                         bool njm_enabled = true) {
  uint32_t r8d = src_bits;
  float input = BitsToFloat(r8d);
  uint32_t ecx = r8d & 0x7FFFFFu;
  uint32_t edx = ecx;
  int32_t eax = 0;
  uint32_t r9d = 0;

  if (r8d == 0xFF800000u) {
    return 0x7FC00000u;
  }

  if ((r8d & 0x7F800000u) == 0 && ecx != 0) {
    if (!njm_enabled) {
      r9d = r8d & 0x7FFFFFFFu;
      if (r9d == 0x00400000u) {
        return ((~r8d >> 31) != 0) ? 0x5F34FD00u : 0x7FC00000u;
      }
      ecx = CountLeadingZeros32(ecx);
      r9d = 9;
      eax = -118;
      edx = static_cast<uint32_t>(static_cast<int32_t>(ecx) - 8);
      r9d -= ecx;
      eax -= static_cast<int32_t>(ecx);
      edx = r8d << edx;
      edx &= 0x7FFFFEu;
      goto compute_from_fields;
    }
    return ((~r8d >> 31) != 0) ? 0x7F800000u : 0xFF800000u;
  }

  r9d = (r8d >> 23) & 0xFFu;
  eax = static_cast<int32_t>(r9d) - 127;
  if (r9d == 255u) {
    if (!(input < 0.0f || std::isnan(input))) {
      if (ecx == 0) {
        return 0u;
      }
    }
    return r8d | 0x00400000u;
  }

compute_from_fields:
  if (eax == 128) {
    if (0.0f > input) {
      if (edx == 0) {
        return 0x7FC00000u;
      }
      return r8d | 0x00400000u;
    }
  }

  if (edx == 0) {
    if (r9d == 0u) {
      return ((~r8d >> 31) != 0) ? 0x7F800000u : 0xFF800000u;
    }
  } else if (eax == 128) {
    return r8d | 0x00400000u;
  }

  if (0.0f > input) {
    return 0x7FC00000u;
  }

  ecx = 127;
  eax <<= 4;
  ecx -= static_cast<int32_t>(r9d);
  r9d = edx;
  eax &= 16;
  edx >>= 9;
  r9d >>= 19;
  edx &= 1023;
  ecx >>= 1;
  eax |= static_cast<int32_t>(r9d);
  eax ^= 16;

  uint32_t table_entry = kVrsqrteTable[static_cast<uint32_t>(eax) & 31];
  eax = static_cast<int32_t>(table_entry);
  r9d = table_entry >> 16;
  edx *= r9d;
  eax <<= 10;
  eax &= 0x3FFFC00;
  eax -= static_cast<int32_t>(edx);
  if (((static_cast<uint32_t>(eax) >> 25) & 1u) == 0) {
    edx = static_cast<uint32_t>(eax) & 0x1FFFFFFu;
    ecx += 6;
    uint32_t lz = CountLeadingZeros32(edx);
    r9d = lz - 6;
    ecx -= static_cast<int32_t>(lz);
    eax = static_cast<int32_t>(static_cast<uint32_t>(eax) << r9d);
  }

  if ((eax & 0x5) != 0 && (eax & 0x2) != 0) {
    eax += 4;
  }

  ecx <<= 23;
  r8d &= 0x80000000u;
  eax = static_cast<int32_t>(static_cast<uint32_t>(eax) >> 2);
  ecx += 0x3F800000;
  eax &= 0x7FFFFF;
  uint32_t out = static_cast<uint32_t>(ecx) | r8d | static_cast<uint32_t>(eax);
  // Apply DAZ/FTZ behavior used by PPC estimate path.
  if ((out & 0x7F800000u) == 0 && (out & 0x007FFFFFu) != 0) {
    out &= 0x80000000u;
  }
  return out;
}

inline uint64_t EmulateFrsqrteBits(uint64_t src_bits,
                                   bool non_ieee_mode = false) {
  uint64_t rax = src_bits;
  constexpr uint64_t kSignMask = 0x8000000000000000ULL;
  constexpr uint64_t kExpMask = 0x7FF0000000000000ULL;

  if (non_ieee_mode) {
    uint64_t shifted = rax << 12;
    if (shifted != 0 && (rax & kExpMask) == 0) {
      return (rax & kSignMask) | kExpMask;
    }
  }

  uint64_t rcx = rax + rax;
  if (rcx == 0) {
    return (rax & kSignMask) | kExpMask;
  }

  if (((~rax) & kExpMask) == 0) {
    if (rax == kExpMask) {
      return 0ULL;
    }
    uint64_t shifted = rax << 12;
    if (shifted == 0 && BitsToDouble(rax) < 0.0) {
      return 0x7FF8000000000000ULL;
    }
    return rax | 0x7FF8000000000000ULL;
  }

  if (0.0 > BitsToDouble(rax)) {
    return 0x7FF8000000000000ULL;
  }

  uint64_t exponent = (rax >> 52) & 0x7FFULL;
  uint64_t mantissa = rax & 0x000FFFFFFFFFFFFFULL;

  if (mantissa != 0 && exponent == 0) {
    uint32_t lz = CountLeadingZeros64(mantissa);
    mantissa <<= static_cast<uint32_t>(static_cast<int32_t>(lz) - 11);
    exponent = static_cast<uint64_t>(12 - static_cast<int32_t>(lz));
  }

  uint32_t edx = static_cast<uint32_t>(exponent * 8);
  uint32_t eax = static_cast<uint32_t>(mantissa >> 49) & 7u;
  int32_t exp_unbiased = static_cast<int32_t>(exponent) - 1023;
  edx &= 8u;
  exp_unbiased >>= 1;
  eax |= edx;
  edx = static_cast<uint32_t>(1022 - exp_unbiased);
  eax ^= 8u;
  uint64_t out = (static_cast<uint64_t>(edx) << 52) |
                 (static_cast<uint64_t>(kFrsqrteTable[eax & 15u]) << 44);
  return out;
}

inline float64x2_t EmulateRsqrtF64(void* raw_context, std::byte src[16]) {
  bool non_ieee_mode = false;
  if (raw_context) {
    auto* backend_context = reinterpret_cast<A64BackendContext*>(
        reinterpret_cast<std::byte*>(raw_context) - sizeof(A64BackendContext));
    non_ieee_mode = backend_context->non_ieee_mode != 0;
  }
  uint64_t in_bits;
  std::memcpy(&in_bits, src, sizeof(in_bits));
  uint64_t out_bits = EmulateFrsqrteBits(in_bits, non_ieee_mode);
  uint64x2_t out = vdupq_n_u64(0);
  out = vsetq_lane_u64(out_bits, out, 0);
  return vreinterpretq_f64_u64(out);
}

inline float32x4_t EmulateRsqrtV128(void* raw_context, std::byte src[16]) {
  bool njm_enabled = true;
  if (raw_context) {
    auto* backend_context = reinterpret_cast<A64BackendContext*>(
        reinterpret_cast<std::byte*>(raw_context) - sizeof(A64BackendContext));
    njm_enabled = backend_context->njm_enabled != 0;
  }
  alignas(16) uint32_t lanes[4];
  std::memcpy(lanes, src, sizeof(lanes));
  for (uint32_t& lane : lanes) {
    lane = EmulateVrsqrteScalarBits(lane, njm_enabled);
  }
  return vreinterpretq_f32_u32(vld1q_u32(lanes));
}

// Altivec guarantees an error of < 1/4096 for vrsqrtefp
struct RSQRT_F32 : Sequence<RSQRT_F32, I<OPCODE_RSQRT, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg src1 = S0;
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    } else {
      src1 = i.src1.reg();
    }
    e.FRSQRTE(i.dest, src1);
  }
};
struct RSQRT_F64 : Sequence<RSQRT_F64, I<OPCODE_RSQRT, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.ADD(e.GetNativeParam(0), SP, e.StashConstantV(0, i.src1.constant()));
    } else {
      e.ADD(e.GetNativeParam(0), SP, e.StashV(0, i.src1.reg().toQ()));
    }
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateRsqrtF64));
    e.FMOV(i.dest, D0);
  }
};
struct RSQRT_V128 : Sequence<RSQRT_V128, I<OPCODE_RSQRT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.ADD(e.GetNativeParam(0), SP, e.StashConstantV(0, i.src1.constant()));
    } else {
      e.ADD(e.GetNativeParam(0), SP, e.StashV(0, i.src1.reg().toQ()));
    }
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateRsqrtV128));
    e.MOV(i.dest.reg().B16(), Q0.B16());
  }
};
struct IS_NAN_F64 : Sequence<IS_NAN_F64, I<OPCODE_IS_NAN, I8Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fcmp(e.d0, e.d0);
    } else {
      e.fcmp(i.src1, i.src1);
    }
    e.cset(i.dest, Xbyak_aarch64::VS);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_NAN, IS_NAN_F32, IS_NAN_F64);

// ============================================================================
// OPCODE_COMPARE_EQ/NE for float
// ============================================================================
// Altivec guarantees an error of < 1/4096 for vrefp
struct RECIP_F32 : Sequence<RECIP_F32, I<OPCODE_RECIP, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg src1 = S0;
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    } else {
      src1 = i.src1.reg();
    }
    SReg one = src1.index() == 0 ? S1 : S0;
    e.MOV(W2, 0x3F800000u);
    e.FMOV(one, W2);
    e.FDIV(i.dest, one, src1);
  }
};
struct RECIP_F64 : Sequence<RECIP_F64, I<OPCODE_RECIP, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg src1 = D0;
    if (i.src1.is_constant) {
      e.LoadConstantV(src1.toQ(), i.src1.constant());
    } else {
      src1 = i.src1.reg();
    }
    DReg one = src1.index() == 0 ? D1 : D0;
    e.MOV(X2, 0x3FF0000000000000ULL);
    e.FMOV(one, X2);
    e.FDIV(i.dest, one, src1);
  }
};
struct RECIP_V128 : Sequence<RECIP_V128, I<OPCODE_RECIP, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      QReg src = i.src1.is_constant ? Q0 : i.src1.reg();
      if (i.src1.is_constant) {
        e.LoadConstantV(src, i.src1.constant());
      }
      QReg one = src.index() == 0 ? Q1 : Q0;
      e.FMOV(one.S4(), FImm8(0, 7, 0));
      e.FDIV(i.dest.reg().S4(), one.S4(), src.S4());
    });
  }
};
struct COMPARE_EQ_F64
    : Sequence<COMPARE_EQ_F64, I<OPCODE_COMPARE_EQ, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      if (i.src2.is_constant) {
        union {
          double d;
          uint64_t u;
        } c2;
        c2.d = i.src2.constant();
        e.mov(e.x0, c2.u);
        e.fmov(e.d1, e.x0);
        e.fcmp(e.d0, e.d1);
      } else {
        e.fcmp(e.d0, i.src2);
      }
    } else if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fcmp(i.src1, e.d0);
    } else {
      e.fcmp(i.src1, i.src2);
    }
    e.cset(i.dest, Xbyak_aarch64::EQ);
  }
};

struct COMPARE_NE_F32
    : Sequence<COMPARE_NE_F32, I<OPCODE_COMPARE_NE, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      if (i.src2.is_constant) {
        union {
          float f;
          uint32_t u;
        } c2;
        c2.f = i.src2.constant();
        e.mov(e.w0, static_cast<uint64_t>(c2.u));
        e.fmov(e.s1, e.w0);
        e.fcmp(e.s0, e.s1);
      } else {
        e.fcmp(e.s0, i.src2);
      }
    } else if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fcmp(i.src1, e.s0);
    } else {
      e.fcmp(i.src1, i.src2);
    }
    e.cset(i.dest, Xbyak_aarch64::NE);
  }
};
struct COMPARE_NE_F64
    : Sequence<COMPARE_NE_F64, I<OPCODE_COMPARE_NE, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      if (i.src2.is_constant) {
        union {
          double d;
          uint64_t u;
        } c2;
        c2.d = i.src2.constant();
        e.mov(e.x0, c2.u);
        e.fmov(e.d1, e.x0);
        e.fcmp(e.d0, e.d1);
      } else {
        e.fcmp(e.d0, i.src2);
      }
    } else if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fcmp(i.src1, e.d0);
    } else {
      e.fcmp(i.src1, i.src2);
    }
    e.cset(i.dest, Xbyak_aarch64::NE);
  }
};

// Float compares for SLT/SLE/SGT/SGE (use MI/LS/GT/GE for ordered compares)
#define DEFINE_FLOAT_COMPARE(NAME, COND_S, COND_D)                   \
  struct NAME##_F32                                                  \
      : Sequence<NAME##_F32, I<OPCODE_##NAME, I8Op, F32Op, F32Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {          \
      if (i.src1.is_constant) {                                      \
        union {                                                      \
          float f;                                                   \
          uint32_t u;                                                \
        } c;                                                         \
        c.f = i.src1.constant();                                     \
        e.mov(e.w0, static_cast<uint64_t>(c.u));                     \
        e.fmov(e.s0, e.w0);                                          \
        if (i.src2.is_constant) {                                    \
          union {                                                    \
            float f;                                                 \
            uint32_t u;                                              \
          } c2;                                                      \
          c2.f = i.src2.constant();                                  \
          e.mov(e.w0, static_cast<uint64_t>(c2.u));                  \
          e.fmov(e.s1, e.w0);                                        \
          e.fcmp(e.s0, e.s1);                                        \
        } else {                                                     \
          e.fcmp(e.s0, i.src2);                                      \
        }                                                            \
      } else if (i.src2.is_constant) {                               \
        union {                                                      \
          float f;                                                   \
          uint32_t u;                                                \
        } c;                                                         \
        c.f = i.src2.constant();                                     \
        e.mov(e.w0, static_cast<uint64_t>(c.u));                     \
        e.fmov(e.s0, e.w0);                                          \
        e.fcmp(i.src1, e.s0);                                        \
      } else {                                                       \
        e.fcmp(i.src1, i.src2);                                      \
      }                                                              \
      e.cset(i.dest, Xbyak_aarch64::COND_S);                         \
    }                                                                \
  };                                                                 \
  struct NAME##_F64                                                  \
      : Sequence<NAME##_F64, I<OPCODE_##NAME, I8Op, F64Op, F64Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {          \
      if (i.src1.is_constant) {                                      \
        union {                                                      \
          double d;                                                  \
          uint64_t u;                                                \
        } c;                                                         \
        c.d = i.src1.constant();                                     \
        e.mov(e.x0, c.u);                                            \
        e.fmov(e.d0, e.x0);                                          \
        if (i.src2.is_constant) {                                    \
          union {                                                    \
            double d;                                                \
            uint64_t u;                                              \
          } c2;                                                      \
          c2.d = i.src2.constant();                                  \
          e.mov(e.x0, c2.u);                                         \
          e.fmov(e.d1, e.x0);                                        \
          e.fcmp(e.d0, e.d1);                                        \
        } else {                                                     \
          e.fcmp(e.d0, i.src2);                                      \
        }                                                            \
      } else if (i.src2.is_constant) {                               \
        union {                                                      \
          double d;                                                  \
          uint64_t u;                                                \
        } c;                                                         \
        c.d = i.src2.constant();                                     \
        e.mov(e.x0, c.u);                                            \
        e.fmov(e.d0, e.x0);                                          \
        e.fcmp(i.src1, e.d0);                                        \
      } else {                                                       \
        e.fcmp(i.src1, i.src2);                                      \
      }                                                              \
      e.cset(i.dest, Xbyak_aarch64::COND_D);                         \
    }                                                                \
  }

DEFINE_FLOAT_COMPARE(COMPARE_SLT, MI, MI);
DEFINE_FLOAT_COMPARE(COMPARE_SLE, LS, LS);
DEFINE_FLOAT_COMPARE(COMPARE_SGT, GT, GT);
DEFINE_FLOAT_COMPARE(COMPARE_SGE, GE, GE);
// For fcmp: LT = N!=V = "less than or unordered" (correct for ULT on floats).
DEFINE_FLOAT_COMPARE(COMPARE_ULT, LT, LT);
// For fcmp: LE = Z=1 or N!=V = "less/equal or unordered" (correct for ULE on
// floats).
DEFINE_FLOAT_COMPARE(COMPARE_ULE, LE, LE);
DEFINE_FLOAT_COMPARE(COMPARE_UGT, HI, HI);
DEFINE_FLOAT_COMPARE(COMPARE_UGE, HS, HS);
#undef DEFINE_FLOAT_COMPARE

// Register all compare opcodes with integer + float variants.
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_EQ, COMPARE_EQ_I8, COMPARE_EQ_I16,
                     COMPARE_EQ_I32, COMPARE_EQ_I64, COMPARE_EQ_F32,
                     COMPARE_EQ_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_NE, COMPARE_NE_I8, COMPARE_NE_I16,
                     COMPARE_NE_I32, COMPARE_NE_I64, COMPARE_NE_F32,
                     COMPARE_NE_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SLT, COMPARE_SLT_I8, COMPARE_SLT_I16,
                     COMPARE_SLT_I32, COMPARE_SLT_I64, COMPARE_SLT_F32,
                     COMPARE_SLT_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SLE, COMPARE_SLE_I8, COMPARE_SLE_I16,
                     COMPARE_SLE_I32, COMPARE_SLE_I64, COMPARE_SLE_F32,
                     COMPARE_SLE_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SGT, COMPARE_SGT_I8, COMPARE_SGT_I16,
                     COMPARE_SGT_I32, COMPARE_SGT_I64, COMPARE_SGT_F32,
                     COMPARE_SGT_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SGE, COMPARE_SGE_I8, COMPARE_SGE_I16,
                     COMPARE_SGE_I32, COMPARE_SGE_I64, COMPARE_SGE_F32,
                     COMPARE_SGE_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_ULT, COMPARE_ULT_I8, COMPARE_ULT_I16,
                     COMPARE_ULT_I32, COMPARE_ULT_I64, COMPARE_ULT_F32,
                     COMPARE_ULT_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_ULE, COMPARE_ULE_I8, COMPARE_ULE_I16,
                     COMPARE_ULE_I32, COMPARE_ULE_I64, COMPARE_ULE_F32,
                     COMPARE_ULE_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_UGT, COMPARE_UGT_I8, COMPARE_UGT_I16,
                     COMPARE_UGT_I32, COMPARE_UGT_I64, COMPARE_UGT_F32,
                     COMPARE_UGT_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_UGE, COMPARE_UGE_I8, COMPARE_UGE_I16,
                     COMPARE_UGE_I32, COMPARE_UGE_I64, COMPARE_UGE_F32,
                     COMPARE_UGE_F64);

// ============================================================================
// OPCODE_MUL_ADD (fused multiply-add)
// ============================================================================
struct MUL_ADD_F32
    : Sequence<MUL_ADD_F32, I<OPCODE_MUL_ADD, F32Op, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 * src2 + src3
    // ARM64: fmadd dest, src1, src2, src3
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    SReg s3 = i.src3.is_constant ? e.s2 : SReg(i.src3.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    if (i.src3.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src3.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s2, e.w0);
    }
    EmitFmaWithPpcNan_F32(e, i.dest, s1, s2, s3, /*is_sub=*/false);
  }
};
struct MUL_ADD_F64
    : Sequence<MUL_ADD_F64, I<OPCODE_MUL_ADD, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    DReg s3 = i.src3.is_constant ? e.d2 : DReg(i.src3.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    if (i.src3.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src3.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d2, e.x0);
    }
    EmitFmaWithPpcNan_F64(e, i.dest, s1, s2, s3, /*is_sub=*/false);
  }
};
struct MUL_ADD_V128
    : Sequence<MUL_ADD_V128,
               I<OPCODE_MUL_ADD, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = s1*s2 + s3 with VMX denormal flushing + PPC NaN propagation.
    // Scratch register plan:
    //   1. Flush s3 into v3, save to stack[32].
    //   2. Flush s1/s2 into v0/v1, save to stack[0]/stack[16].
    //   3. Restore s3 into v3, fmla into v2, NaN fixup, flush output.
    EmitWithVmxFpcr(e, [&] {
      int d = i.dest.reg().getIdx();

      // Flush s3 → v3, save to stack slot 2.
      int s3 = SrcVReg(e, i.src3, 3);
      if (s3 != 3) e.mov(VReg(3).b16, VReg(s3).b16);
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 3, 0, 1);
      }
      e.str(QReg(3),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));

      // Flush s1/s2 → v0/v1, save to stack slots 0/1.
      int s1, s2;
      PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
      e.str(QReg(0), Xbyak_aarch64::ptr(e.sp, static_cast<int32_t>(
                                                  StackLayout::GUEST_SCRATCH)));
      e.str(QReg(1),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 16));

      // Restore flushed s3, compute fmla into v2 via copy.
      e.ldr(QReg(2),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));
      e.fmla(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);

      // PPC NaN fixup (sources on stack at offsets 0/16/32).
      FixupVmxNan_V128_Fma(e);

      // Flush output denormals.
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 2, 0, 1);
      }
      e.mov(VReg(d).b16, VReg(2).b16);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_ADD, MUL_ADD_F32, MUL_ADD_F64, MUL_ADD_V128);

// ============================================================================
// OPCODE_MUL_SUB (fused multiply-subtract)
// ============================================================================
struct MUL_SUB_F64
    : Sequence<MUL_SUB_F64, I<OPCODE_MUL_SUB, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 * src2 - src3
    // ARM64 fnmsub(d,n,m,a) = -a + n*m = n*m - a
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    DReg s3 = i.src3.is_constant ? e.d2 : DReg(i.src3.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    if (i.src3.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src3.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d2, e.x0);
    }
    EmitFmaWithPpcNan_F64(e, i.dest, s1, s2, s3, /*is_sub=*/true);
  }
};
struct MUL_SUB_V128
    : Sequence<MUL_SUB_V128,
               I<OPCODE_MUL_SUB, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = s1*s2 - s3 with VMX denormal flushing + PPC NaN propagation.
    // Same as MUL_ADD but negate s3 before the fmla.
    EmitWithVmxFpcr(e, [&] {
      int d = i.dest.reg().getIdx();

      // Flush s3 → v3, save un-negated for NaN fixup.
      int s3 = SrcVReg(e, i.src3, 3);
      if (s3 != 3) e.mov(VReg(3).b16, VReg(s3).b16);
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 3, 0, 1);
      }
      e.str(QReg(3),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));

      // Flush s1/s2 → v0/v1, save for NaN fixup.
      int s1, s2;
      PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
      e.str(QReg(0), Xbyak_aarch64::ptr(e.sp, static_cast<int32_t>(
                                                  StackLayout::GUEST_SCRATCH)));
      e.str(QReg(1),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 16));

      // Reload flushed s3, negate into v2, fmla: v2 = -s3 + s1*s2 = s1*s2 - s3.
      e.ldr(QReg(2),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));
      e.fneg(VReg(2).s4, VReg(2).s4);
      e.fmla(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);

      // PPC NaN fixup (sources on stack at offsets 0/16/32).
      FixupVmxNan_V128_Fma(e);

      // Flush output denormals.
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 2, 0, 1);
      }
      e.mov(VReg(d).b16, VReg(2).b16);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_SUB, MUL_SUB_F64, MUL_SUB_V128);

// ============================================================================
// POW2 / LOG2 / DOT_PRODUCT C helper functions (called via CallNativeSafe)
// ============================================================================

// POW2 (vexptefp): 2^x for each of 4 float lanes.
// Args: x0=PPCContext* (unused), x1=pointer to vec128_t (in-place).
static void EmulatePow2(void* /*ctx*/, void* vdata) {
  auto* data = reinterpret_cast<vec128_t*>(vdata);
  for (int i = 0; i < 4; i++) {
    data->f32[i] = std::exp2(data->f32[i]);
  }
}

// LOG2 (vlogefp): log2(x) for each of 4 float lanes.
// Args: x0=PPCContext* (unused), x1=pointer to vec128_t (in-place).
static void EmulateLog2(void* /*ctx*/, void* vdata) {
  auto* data = reinterpret_cast<vec128_t*>(vdata);
  for (int i = 0; i < 4; i++) {
    data->f32[i] = std::log2(data->f32[i]);
  }
}

// DOT_PRODUCT_3 (vmsum3fp): dot product of first 3 elements.
// Uses double-precision intermediates; overflow -> QNaN.
// Args: x0=PPCContext* (unused), x1=pointer to 2 consecutive vec128_t
//       (src1 at offset 0, src2 at offset 16). Result stored in src1.
static void EmulateDotProduct3(void* /*ctx*/, void* vdata) {
  auto* data = reinterpret_cast<vec128_t*>(vdata);
  vec128_t& src1 = data[0];
  vec128_t& src2 = data[1];
  double d0 = (double)src1.f32[0] * (double)src2.f32[0];
  double d1 = (double)src1.f32[1] * (double)src2.f32[1];
  double d2 = (double)src1.f32[2] * (double)src2.f32[2];
  double sum = d0 + d1 + d2;
  float result = (float)sum;
  if (std::isinf(result)) {
    uint32_t qnan = 0x7FC00000u;
    memcpy(&result, &qnan, sizeof(result));
  }
  src1.f32[0] = src1.f32[1] = src1.f32[2] = src1.f32[3] = result;
}

// DOT_PRODUCT_4 (vmsum4fp): dot product of all 4 elements.
// Uses double-precision intermediates; overflow -> QNaN.
// Args: x0=PPCContext* (unused), x1=pointer to 2 consecutive vec128_t.
static void EmulateDotProduct4(void* /*ctx*/, void* vdata) {
  auto* data = reinterpret_cast<vec128_t*>(vdata);
  vec128_t& src1 = data[0];
  vec128_t& src2 = data[1];
  double d0 = (double)src1.f32[0] * (double)src2.f32[0];
  double d1 = (double)src1.f32[1] * (double)src2.f32[1];
  double d2 = (double)src1.f32[2] * (double)src2.f32[2];
  double d3 = (double)src1.f32[3] * (double)src2.f32[3];
  double sum = d0 + d1 + d2 + d3;
  float result = (float)sum;
  if (std::isinf(result)) {
    uint32_t qnan = 0x7FC00000u;
    memcpy(&result, &qnan, sizeof(result));
  }
  src1.f32[0] = src1.f32[1] = src1.f32[2] = src1.f32[3] = result;
}

// ============================================================================
// OPCODE_POW2
// ============================================================================
struct POW2_F32 : Sequence<POW2_F32, I<OPCODE_POW2, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_always("POW2_F32 should not be emitted");
  }
};
struct POW2_F64 : Sequence<POW2_F64, I<OPCODE_POW2, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_always("POW2_F64 should not be emitted");
  }
};
struct POW2_V128 : Sequence<POW2_V128, I<OPCODE_POW2, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // No hardware FP emitted — the C++ helper does all math.
    // GuestToHostThunk restores FPCR after the native call.
    int s = SrcVReg(e, i.src1, 0);
    int d = i.dest.reg().getIdx();
    e.str(QReg(s),
          ptr(e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
    e.add(e.x1, e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.ldr(QReg(d),
          ptr(e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_POW2, POW2_F32, POW2_F64, POW2_V128);

// ============================================================================
// OPCODE_LOG2
// ============================================================================
struct LOG2_F32 : Sequence<LOG2_F32, I<OPCODE_LOG2, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_always("LOG2_F32 should not be emitted");
  }
};
struct LOG2_F64 : Sequence<LOG2_F64, I<OPCODE_LOG2, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_always("LOG2_F64 should not be emitted");
  }
};
struct LOG2_V128 : Sequence<LOG2_V128, I<OPCODE_LOG2, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // No hardware FP emitted — the C++ helper does all math.
    // GuestToHostThunk restores FPCR after the native call.
    int s = SrcVReg(e, i.src1, 0);
    int d = i.dest.reg().getIdx();
    e.str(QReg(s),
          ptr(e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
    e.add(e.x1, e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.ldr(QReg(d),
          ptr(e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOG2, LOG2_F32, LOG2_F64, LOG2_V128);

// ============================================================================
// OPCODE_DOT_PRODUCT_3
// ============================================================================
inline uint32_t EmulateDotProductBits(const std::byte src1[16],
                                      const std::byte src2[16], bool dot3) {
  alignas(16) uint32_t lhs_bits[4];
  alignas(16) uint32_t rhs_bits[4];
  std::memcpy(lhs_bits, src1, sizeof(lhs_bits));
  std::memcpy(rhs_bits, src2, sizeof(rhs_bits));

  if (dot3) {
    lhs_bits[3] = 0;
    rhs_bits[3] = 0;
  }

  const double p0 = static_cast<double>(BitsToFloat(lhs_bits[0])) *
                    static_cast<double>(BitsToFloat(rhs_bits[0]));
  const double p1 = static_cast<double>(BitsToFloat(lhs_bits[1])) *
                    static_cast<double>(BitsToFloat(rhs_bits[1]));
  const double p2 = static_cast<double>(BitsToFloat(lhs_bits[2])) *
                    static_cast<double>(BitsToFloat(rhs_bits[2]));
  const double p3 = static_cast<double>(BitsToFloat(lhs_bits[3])) *
                    static_cast<double>(BitsToFloat(rhs_bits[3]));

  const double sum = dot3 ? ((p0 + p2) + p1) : ((p0 + p2) + (p1 + p3));
  if (std::isfinite(sum) &&
      std::fabs(sum) > static_cast<double>(std::numeric_limits<float>::max())) {
    return 0x7FC00000u;
  }

  float result = static_cast<float>(sum);
  uint32_t result_bits;
  std::memcpy(&result_bits, &result, sizeof(result_bits));
  return result_bits;
}

inline float32x4_t EmulateDotProduct3V128(void*, std::byte src1[16],
                                          std::byte src2[16]) {
  uint32_t result_bits = EmulateDotProductBits(src1, src2, true);
  return vreinterpretq_f32_u32(vdupq_n_u32(result_bits));
}

struct DOT_PRODUCT_3_V128
    : Sequence<DOT_PRODUCT_3_V128,
               I<OPCODE_DOT_PRODUCT_3, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.ADD(e.GetNativeParam(0), SP, e.StashConstantV(0, i.src1.constant()));
    } else {
      e.ADD(e.GetNativeParam(0), SP, e.StashV(0, i.src1.reg().toQ()));
    }
    if (i.src2.is_constant) {
      e.ADD(e.GetNativeParam(1), SP, e.StashConstantV(1, i.src2.constant()));
    } else {
      e.ADD(e.GetNativeParam(1), SP, e.StashV(1, i.src2.reg().toQ()));
    }
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateDotProduct3V128));
    e.MOV(i.dest.reg().B16(), Q0.B16());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_3, DOT_PRODUCT_3_V128);

// ============================================================================
// OPCODE_DOT_PRODUCT_4
// ============================================================================
inline float32x4_t EmulateDotProduct4V128(void*, std::byte src1[16],
                                          std::byte src2[16]) {
  uint32_t result_bits = EmulateDotProductBits(src1, src2, false);
  return vreinterpretq_f32_u32(vdupq_n_u32(result_bits));
}

struct DOT_PRODUCT_4_V128
    : Sequence<DOT_PRODUCT_4_V128,
               I<OPCODE_DOT_PRODUCT_4, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.ADD(e.GetNativeParam(0), SP, e.StashConstantV(0, i.src1.constant()));
    } else {
      e.ADD(e.GetNativeParam(0), SP, e.StashV(0, i.src1.reg().toQ()));
    }
    if (i.src2.is_constant) {
      e.ADD(e.GetNativeParam(1), SP, e.StashConstantV(1, i.src2.constant()));
    } else {
      e.ADD(e.GetNativeParam(1), SP, e.StashV(1, i.src2.reg().toQ()));
    }
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateDotProduct4V128));
    e.MOV(i.dest.reg().B16(), Q0.B16());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_4, DOT_PRODUCT_4_V128);

// ============================================================================
// OPCODE_SET_ROUNDING_MODE
// ============================================================================
// PPC rounding mode (input bits 0-2) to ARM64 FPCR value table.
// Bits 0-1: PPC RN (rounding mode), Bit 2: PPC NI (non-IEEE / flush-to-zero).
//   PPC RN=0 (nearest) -> ARM64 RMode=00, PPC RN=1 (toward zero) -> RMode=11,
//   PPC RN=2 (toward +inf) -> RMode=01, PPC RN=3 (toward -inf) -> RMode=10.
// ARM64 FPCR RMode is bits 23:22, FZ is bit 24.
// Index 0-3: NI=0 (IEEE), Index 4-7: NI=1 (non-IEEE, FZ set).
static constexpr uint32_t fpcr_table[8] = {
    (0b00 << 22),              // PPC 0: nearest, IEEE
    (0b11 << 22),              // PPC 1: toward zero, IEEE
    (0b01 << 22),              // PPC 2: toward +inf, IEEE
    (0b10 << 22),              // PPC 3: toward -inf, IEEE
    (0b00 << 22) | (1 << 24),  // PPC 4: nearest, flush-to-zero
    (0b11 << 22) | (1 << 24),  // PPC 5: toward zero, flush-to-zero
    (0b01 << 22) | (1 << 24),  // PPC 6: toward +inf, flush-to-zero
    (0b10 << 22) | (1 << 24),  // PPC 7: toward -inf, flush-to-zero
};
struct SET_ROUNDING_MODE
    : Sequence<SET_ROUNDING_MODE, I<OPCODE_SET_ROUNDING_MODE, VoidOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Input is PPC FPSCR bits (already masked to 0-7 by the frontend).
    // We set FPCR RMode + FZ bits and cache the value in the backend context.
    auto bctx = e.GetBackendCtxReg();

    if (i.src1.is_constant) {
      uint32_t fpcr_val = fpcr_table[i.src1.constant() & 7];
      e.mov(e.x0, static_cast<uint64_t>(fpcr_val));
      e.msr(3, 3, 4, 4, 0, e.x0);  // msr FPCR, x0
      // Cache in backend context.
      e.str(e.w0, ptr(bctx, static_cast<uint32_t>(
                                offsetof(A64BackendContext, fpcr_fpu))));
      // Update NonIEEE flag.
      e.ldr(
          e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
      if (i.src1.constant() & 4) {
        e.orr(e.w0, e.w0, 1u << kA64BackendNonIEEEMode);
      } else {
        // Clear bit kA64BackendNonIEEEMode using BIC (avoids bitmask encoding).
        e.mov(e.w1, 1u << kA64BackendNonIEEEMode);
        e.bic(e.w0, e.w0, e.w1);
      }
      e.str(
          e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    } else {
      // Dynamic: look up FPCR value from table.
      e.mov(e.x0, reinterpret_cast<uint64_t>(fpcr_table));
      e.and_(e.w1, i.src1, 7);
      e.ldr(e.w0, Xbyak_aarch64::ptr(e.x0, e.x1, Xbyak_aarch64::LSL, 2));
      // Write FPCR.
      e.msr(3, 3, 4, 4, 0, e.x0);
      // Cache in backend context.
      e.str(e.w0, ptr(bctx, static_cast<uint32_t>(
                                offsetof(A64BackendContext, fpcr_fpu))));
      // Update NonIEEE flag based on bit 2 of input.
      e.ldr(
          e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
      // Clear bit kA64BackendNonIEEEMode using BIC (avoids bitmask encoding).
      e.mov(e.w1, 1u << kA64BackendNonIEEEMode);
      e.bic(e.w0, e.w0, e.w1);
      // Conditionally set it back if input bit 2 is set.
      e.tst(i.src1, 4);
      e.csel(e.w1, e.w1, e.wzr, Xbyak_aarch64::Cond::NE);
      e.orr(e.w0, e.w0, e.w1);
      e.str(
          e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    }
    e.ChangeFpcrMode(FPCRMode::Fpu, /*already_set=*/true);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SET_ROUNDING_MODE, SET_ROUNDING_MODE);

// PPC frsqrte lookup table implementation (PowerISA Table E-5).
// Matches the x64 backend's EmitFrsqrteHelper.
static uint64_t PpcFrsqrte(uint64_t bits) {
  uint32_t sign = (uint32_t)(bits >> 63);
  uint32_t exp = (uint32_t)((bits >> 52) & 0x7FF);
  uint64_t mantissa = bits & 0x000FFFFFFFFFFFFFULL;

  // NaN → QNaN (quiet it, preserve sign and payload)
  if (exp == 0x7FF && mantissa != 0) {
    return bits | (1ULL << 51);
  }
  // ±0 → ±inf
  if (exp == 0 && mantissa == 0) {
    return sign ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL;
  }
  // +inf → +0
  if (exp == 0x7FF && !sign) {
    return 0;
  }
  // -inf or negative → QNaN
  if (sign) {
    return 0x7FF8000000000000ULL;
  }

  // Denormal: normalize (matching x64 EmitFrsqrteHelper L25).
  int32_t effective_exp = (int32_t)exp;
  uint64_t norm_mantissa = mantissa;
  if (exp == 0) {
    int lz = (int)xe::lzcnt(mantissa);  // leading zeros in 64-bit
    norm_mantissa = mantissa << (lz - 11);
    effective_exp = 12 - lz;
  }

  // PPC frsqrte lookup table (16 entries, 8 bits each).
  static constexpr uint8_t table[] = {241, 216, 192, 168, 152, 136, 128, 112,
                                      96,  76,  60,  48,  32,  24,  16,  8};

  // Index: bit 3 = !(exp & 1), bits 2:0 = top 3 mantissa bits.
  // For denormals, norm_mantissa has implicit 1 at bit 52; & 7 masks it out.
  uint32_t top3 = (uint32_t)(norm_mantissa >> 49) & 7;
  uint32_t index = (((uint32_t)effective_exp & 1) << 3) | top3;
  index ^= 8;

  // Result exponent = 1022 - floor((effective_exp - 1023) / 2).
  int32_t unbiased = effective_exp - 1023;
  int32_t half = unbiased >> 1;  // arithmetic shift = floor division
  uint32_t result_exp = (uint32_t)(1022 - half);

  // Construct result: exponent in bits 62:52, table value in bits 51:44.
  uint64_t result =
      ((uint64_t)result_exp << 52) | ((uint64_t)table[index] << 44);
  return result;
}

// ============================================================================
// OPCODE_RSQRT
// ============================================================================
struct RSQRT_F32 : Sequence<RSQRT_F32, I<OPCODE_RSQRT, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    SReg src = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    e.fsqrt(e.s1, src);
    e.mov(e.w0, static_cast<uint64_t>(0x3F800000u));
    e.fmov(e.s2, e.w0);
    e.fdiv(i.dest, e.s2, e.s1);
  }
};
struct RSQRT_F64 : Sequence<RSQRT_F64, I<OPCODE_RSQRT, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // PPC frsqrte uses a specific lookup table, not a high-precision estimate.
    // Call PpcFrsqrte directly (pure integer math, no FPCR impact).
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    e.fmov(e.x0, src);
    e.mov(e.x9, reinterpret_cast<uint64_t>(PpcFrsqrte));
    e.blr(e.x9);
    e.fmov(i.dest, e.x0);
  }
};
// PPC vrsqrtefp per-lane implementation.
// Uses the same 32-entry lookup table + interpolation as x64's
// EmitScalarVRsqrteHelper.
static uint32_t PpcVrsqrtefpLane(uint32_t bits) {
  static constexpr uint32_t table[32] = {
      0x0568B4FD, 0x04F3AF97, 0x048DAAA5, 0x0435A618, 0x03E7A1E4, 0x03A29DFE,
      0x03659A5C, 0x032E96F8, 0x02FC93CA, 0x02D090CE, 0x02A88DFE, 0x02838B57,
      0x026188D4, 0x02438673, 0x02268431, 0x020B820B, 0x03D27FFA, 0x03807C29,
      0x033878AA, 0x02F97572, 0x02C27279, 0x02926FB7, 0x02666D26, 0x023F6AC0,
      0x021D6881, 0x01FD6665, 0x01E16468, 0x01C76287, 0x01AF60C1, 0x01995F12,
      0x01855D79, 0x01735BF4,
  };

  uint32_t sign = bits >> 31;
  uint32_t biased_exp = (bits >> 23) & 0xFF;
  uint32_t mantissa = bits & 0x007FFFFF;

  // -Inf → QNaN
  if (bits == 0xFF800000u) return 0x7FC00000u;

  // Denormal or zero (exp == 0)
  if (biased_exp == 0) {
    // ±0 or denormal with NJM on → flush to ±0 → ±Inf
    return sign ? 0xFF800000u : 0x7F800000u;
  }

  // NaN/Inf (exp == 255)
  if (biased_exp == 255) {
    if (mantissa == 0) {
      // +Inf → +0 (-Inf already handled above)
      return 0;
    }
    e.ADD(e.GetNativeParam(0), SP, e.StashV(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateShrV128));
    e.MOV(i.dest.reg().B16(), Q0.B16());
  }
  static float32x4_t EmulateShrV128(void*, std::byte src1[16], uint8_t src2) {
    // Almost all instances are shamt = 1, but non-constant.
    // shamt is [0,7]
    uint8_t shamt = src2 & 0x7;
    // Load `src1` as a byte vector (uint8x16_t) and store it into `value`
    uint8x16_t byte_vec = vld1q_u8(reinterpret_cast<const uint8_t*>(src1));
    alignas(16) vec128_t value;
    vst1q_u8(reinterpret_cast<uint8_t*>(&value), byte_vec);

    for (int i = 15; i > 0; --i) {
      value.u8[i ^ 0x3] = (value.u8[i ^ 0x3] >> shamt) |
                          (value.u8[(i - 1) ^ 0x3] << (8 - shamt));
    }
    value.u8[0 ^ 0x3] = value.u8[0 ^ 0x3] >> shamt;
    // Convert to float32x4_t by reinterpreting the processed `value`
    return vreinterpretq_f32_u8(
        vld1q_u8(reinterpret_cast<const uint8_t*>(&value)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHR, SHR_I8, SHR_I16, SHR_I32, SHR_I64, SHR_V128);

// ============================================================================
// OPCODE_SHA
// ============================================================================
struct SHA_I8 : Sequence<SHA_I8, I<OPCODE_SHA, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    Sequence::EmitAssociativeBinaryOp(
        e, i,
        [](A64Emitter& e, WReg dest_src, WReg src) {
          e.SXTB(dest_src, dest_src);
          e.ASR(dest_src, dest_src, src);
        },
        [](A64Emitter& e, WReg dest_src, int8_t constant) {
          e.SXTB(dest_src, dest_src);
          e.ASR(dest_src, dest_src, constant);
        });
  }
};
struct SHA_I16 : Sequence<SHA_I16, I<OPCODE_SHA, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    Sequence::EmitAssociativeBinaryOp(
        e, i,
        [](A64Emitter& e, WReg dest_src, WReg src) {
          e.SXTH(dest_src, dest_src);
          e.ASR(dest_src, dest_src, src);
        },
        [](A64Emitter& e, WReg dest_src, int8_t constant) {
          e.SXTH(dest_src, dest_src);
          e.ASR(dest_src, dest_src, constant);
        });
  }
};
struct SHA_I32 : Sequence<SHA_I32, I<OPCODE_SHA, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    Sequence::EmitAssociativeBinaryOp(
        e, i,
        [](A64Emitter& e, WReg dest_src, WReg src) {
          e.ASR(dest_src, dest_src, src);
        },
        [](A64Emitter& e, WReg dest_src, int8_t constant) {
          e.ASR(dest_src, dest_src, constant);
        });
  }
};
struct SHA_I64 : Sequence<SHA_I64, I<OPCODE_SHA, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    Sequence::EmitAssociativeBinaryOp(
        e, i,
        [](A64Emitter& e, XReg dest_src, WReg src) {
          e.ASR(dest_src, dest_src, src.toX());
        },
        [](A64Emitter& e, XReg dest_src, int8_t constant) {
          e.ASR(dest_src, dest_src, constant);
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHA, SHA_I8, SHA_I16, SHA_I32, SHA_I64);

// ============================================================================
// OPCODE_ROTATE_LEFT
// ============================================================================
// TODO(benvanik): put dest/src1 together, src2 in cl.
template <typename SEQ, typename ARGS>
void EmitRotateLeftNarrow(A64Emitter& e, const ARGS& i, uint32_t width) {
  const uint32_t value_mask = (uint32_t(1) << width) - 1;
  const uint32_t shift_mask = width - 1;

  if (i.src1.is_constant) {
    e.MOV(W0, uint32_t(i.src1.constant()));
  } else {
    e.MOV(W0, i.src1.reg().toW());
  }
  e.MOV(W4, value_mask);
  e.AND(W0, W0, W4);

  if (i.src2.is_constant) {
    e.MOV(W1, uint32_t(i.src2.constant()));
  } else {
    e.MOV(W1, i.src2.reg().toW());
  }
  e.MOV(W4, shift_mask);
  e.AND(W1, W1, W4);

  // left = (value << sh), right = (value >> (width - sh))
  e.MOV(W2, width);
  e.SUB(W2, W2, W1);
  e.LSLV(W3, W0, W1);
  e.LSRV(W1, W0, W2);
  e.ORR(W3, W3, W1);
  e.MOV(W4, value_mask);
  e.AND(i.dest, W3, W4);
}

struct RSQRT_V128 : Sequence<RSQRT_V128, I<OPCODE_RSQRT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftNarrow<ROTATE_LEFT_I8>(e, i, 8);
  }
};
struct ROTATE_LEFT_I16
    : Sequence<ROTATE_LEFT_I16, I<OPCODE_ROTATE_LEFT, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftNarrow<ROTATE_LEFT_I16>(e, i, 16);
  }
};
struct ROTATE_LEFT_I32
    : Sequence<ROTATE_LEFT_I32, I<OPCODE_ROTATE_LEFT, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.MOV(W0, i.src1.constant());
    } else {
      e.MOV(W0, i.src1.reg());
    }

    if (i.src2.is_constant) {
      e.MOV(W1, i.src2.constant());
    } else {
      e.SXTB(W1, i.src2.reg());
    }
    e.NEG(W1, W1);

    e.ROR(i.dest, W0, W1);
  }
};
struct ROTATE_LEFT_I64
    : Sequence<ROTATE_LEFT_I64, I<OPCODE_ROTATE_LEFT, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.MOV(X0, i.src1.constant());
    } else {
      e.MOV(X0, i.src1.reg());
    }

    if (i.src2.is_constant) {
      e.MOV(X1, i.src2.constant());
    } else {
      e.SXTB(X1, i.src2.reg().toW());
    }
    e.NEG(X1, X1);

    e.ROR(i.dest, X0, X1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROTATE_LEFT, ROTATE_LEFT_I8, ROTATE_LEFT_I16,
                     ROTATE_LEFT_I32, ROTATE_LEFT_I64);

// ============================================================================
// OPCODE_RECIP
// ============================================================================
struct RECIP_F32 : Sequence<RECIP_F32, I<OPCODE_RECIP, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    SReg src = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    e.fmov(e.s2, 1.0f);
    e.fdiv(i.dest, e.s2, src);
  }
};
struct RECIP_F64 : Sequence<RECIP_F64, I<OPCODE_RECIP, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    e.fmov(e.d2, 1.0);
    e.fdiv(i.dest, e.d2, src);
  }
};
struct RECIP_V128 : Sequence<RECIP_V128, I<OPCODE_RECIP, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      if (i.src1.is_constant) {
        LoadV128Const(e, 1, i.src1.constant());
      } else {
        e.mov(VReg(1).b16, VReg(i.src1.reg().getIdx()).b16);
      }
      // Flush input denormals.
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 1);  // scratch v2, v3
      }
      auto d = VReg(i.dest.reg().getIdx()).s4;
      // Load 1.0f vector.
      e.fmov(VReg(0).s4, 1.0f);
      e.fdiv(d, VReg(0).s4, VReg(1).s4);
      // Flush output denormals.
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, i.dest.reg().getIdx(), 0, 1);
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RECIP, RECIP_F32, RECIP_F64, RECIP_V128);

// ============================================================================
// OPCODE_TO_SINGLE
// ============================================================================
struct TOSINGLE : Sequence<TOSINGLE, I<OPCODE_TO_SINGLE, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    // Round double->single->double.
    // NaN sign is already correct from upstream arithmetic (EmitFmaWithPpcNan
    // etc.) or fneg.  fcvt with DN=0 preserves NaN sign, so no fixup needed.
    e.fcvt(e.s0, src);
    e.fcvt(i.dest, e.s0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TO_SINGLE, TOSINGLE);

// ============================================================================
// OPCODE_SET_NJM
// ============================================================================
struct SET_NJM : Sequence<SET_NJM, I<OPCODE_SET_NJM, VoidOp, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // NJM (Non-Java Mode) is a VMX/AltiVec feature (VSCR bit 16) that
    // controls flush-to-zero for vector operations.  It does NOT affect
    // scalar FPU behaviour.  On ARM64 this maps to FPCR.FZ (bit 24) in
    // the cached fpcr_vmx value, which EmitWithVmxFpcr loads before
    // each vector FP operation.
    auto bctx = e.GetBackendCtxReg();

    // ============================================================================
    // OPCODE_SET_ROUNDING_MODE
    // ============================================================================
    // Input: FPSCR (PPC format)
    // Convert from PPC rounding mode to ARM
    // PPC | ARM |
    // 00  | 00  | nearest
    // 01  | 11  | toward zero
    // 10  | 01  | toward +infinity
    // 11  | 10  | toward -infinity
    static const uint8_t fpcr_table[] = {
        0b0'00,  // |--|nearest
        0b0'11,  // |--|toward zero
        0b0'01,  // |--|toward +infinity
        0b0'10,  // |--|toward -infinity
        0b1'00,  // |FZ|nearest
        0b1'11,  // |FZ|toward zero
        0b1'01,  // |FZ|toward +infinity
        0b1'10,  // |FZ|toward -infinity
    };

    uint64_t SetNonIEEEModeForwarder(void* raw_context, uint64_t control) {
      control &= 0b111;
      auto* backend_context = reinterpret_cast<A64BackendContext*>(
          reinterpret_cast<std::byte*>(raw_context) -
          sizeof(A64BackendContext));
      backend_context->non_ieee_mode = (control >> 2) & 1;
      return control;
    }

    struct SET_ROUNDING_MODE_I32
        : Sequence<SET_ROUNDING_MODE_I32,
                   I<OPCODE_SET_ROUNDING_MODE, VoidOp, I32Op>> {
      static void Emit(A64Emitter& e, const EmitArgType& i) {
        // Low 3 bits are |Non-IEEE:1|RoundingMode:2|
        // Non-IEEE bit is flush-to-zero
        if (i.src1.is_constant) {
          const uint32_t control =
              static_cast<uint32_t>(i.src1.constant()) & 0b111;
          e.CallNative(SetNonIEEEModeForwarder, control);
          e.MOV(W1, control);
        } else {
          e.AND(W1, i.src1.reg(), 0b111);
          e.CallNativeSafe(reinterpret_cast<void*>(SetNonIEEEModeForwarder));
          e.MOV(W1, W0);
        }

        // Update kA64BackendNJMOn flag.
        e.ldr(e.w0, ptr(bctx, static_cast<uint32_t>(
                                  offsetof(A64BackendContext, flags))));
        if (i.src1.is_constant) {
          if (i.src1.constant()) {
            e.orr(e.w0, e.w0, 1u << kA64BackendNJMOn);
          } else {
            e.mov(e.w1, 1u << kA64BackendNJMOn);
            e.bic(e.w0, e.w0, e.w1);
          }
        } else {
          e.mov(e.w1, 1u << kA64BackendNJMOn);
          e.bic(e.w0, e.w0, e.w1);
          e.tst(i.src1, 0xFF);
          e.csel(e.w1, e.w1, e.wzr, Xbyak_aarch64::Cond::NE);
          e.orr(e.w0, e.w0, e.w1);
        }
        e.str(e.w0, ptr(bctx, static_cast<uint32_t>(
                                  offsetof(A64BackendContext, flags))));

        e.ForgetFpcrMode();
      }
    };
    EMITTER_OPCODE_TABLE(OPCODE_SET_NJM, SET_NJM);

    // Force-link the split sequence files so their static initializers run.
    extern volatile int anchor_control;
    static int anchor_control_dest = anchor_control;

    extern volatile int anchor_memory;
    static int anchor_memory_dest = anchor_memory;

    extern volatile int anchor_vector;
    static int anchor_vector_dest = anchor_vector;

    // ============================================================================
    // SelectSequence — dispatch an instruction to its sequence handler
    // ============================================================================
    bool SelectSequence(A64Emitter * e, const hir::Instr* i,
                        const hir::Instr** new_tail) {
      const InstrKey key(i);
      auto& sequence_table = SequenceTable();
      auto it = sequence_table.find(key);
      if (it != sequence_table.end()) {
        if (it->second(*e, i, InstrKeyValue(key))) {
          *new_tail = i->next;
          return true;
        }
      }
      XELOGE("A64: No sequence match for opcode: {} ({})",
             hir::GetOpcodeName(i->GetOpcodeInfo()),
             static_cast<int>(i->GetOpcodeInfo()->num));
      fprintf(stderr, "A64: No sequence match for opcode: %s (%d)\n",
              hir::GetOpcodeName(i->GetOpcodeInfo()),
              static_cast<int>(i->GetOpcodeInfo()->num));
      return false;
    }

  }  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

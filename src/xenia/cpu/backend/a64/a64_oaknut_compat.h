/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_OAKNUT_COMPAT_H_
#define XENIA_CPU_BACKEND_A64_A64_OAKNUT_COMPAT_H_

// File-local compatibility shims for sequence files that still use the older
// Oaknut-style register aliases and uppercase mnemonic names.
#define Cond Xbyak_aarch64
#define QReg ::xe::cpu::backend::a64::A64VReg

#define X0 XReg(0)
#define X1 XReg(1)
#define X2 XReg(2)
#define X13 XReg(13)
#define X14 XReg(14)
#define W0 WReg(0)
#define W1 WReg(1)
#define W2 WReg(2)
#define W3 WReg(3)
#define W4 WReg(4)
#define WZR WReg(31)
#define XZR XReg(31)
#define SP XReg(31)
#define S0 SReg(0)
#define S1 SReg(1)
#define S2 SReg(2)
#define S3 SReg(3)
#define D0 DReg(0)
#define D1 DReg(1)
#define D2 DReg(2)
#define D3 DReg(3)
#define Q0 QReg(0)
#define Q1 QReg(1)
#define Q2 QReg(2)
#define Q3 QReg(3)
#define Q4 QReg(4)
#define Q6 QReg(6)
#define Q7 QReg(7)

#define MOV mov
#define CMP cmp
#define ADD add
#define B b
#define FMOV fmov
#define CSEL csel
#define CSET cset
#define FCMP fcmp
#define SDIV sdiv
#define ASR asr
#define UDIV udiv
#define MVN mvn
#define FCMEQ fcmeq
#define FCMGE fcmge
#define FCMGT fcmgt
#define BIT bit
#define CBZ cbz
#define FDIV fdiv
#define FSUB fsub
#define BIC bic
#define FCVT fcvt
#define FCVTZU fcvtzu
#define ORR orr
#define AND and_
#define SXTB sxtb
#define FMUL fmul
#define FNEG fneg
#define FRINTP frintp
#define FRINTN frintn
#define FRINTM frintm
#define MRS mrs
#define FRINTZ frintz
#define FMIN fmin
#define FMAX fmax
#define FCVTZS fcvtzs
#define FCVTNS fcvtns
#define FADD fadd
#define FMLA fmla
#define SXTH sxth
#define BSL bsl
#define SCVTF scvtf
#define UCVTF ucvtf
#define ROR ror
#define NEG neg
#define ADC adc
#define FSQRT fsqrt
#define FMADD fmadd
#define FCSEL fcsel
#define UBFX ubfx
#define MUL mul
#define BFI bfi
#define LSRV lsrv
#define DUP dup
#define LSLV lslv
#define FRSQRTE frsqrte
#define CSETM csetm
#define SUB sub
#define CMN cmn
#define MSR msr
#define CMEQ cmeq
#define CMGE cmge
#define CMGT cmgt
#define CMHI cmhi
#define CMHS cmhs
#define FABS fabs
#define SQADD sqadd
#define SQSUB sqsub
#define UQADD uqadd
#define UQSUB uqsub
#define SRHADD srhadd
#define URHADD urhadd

#endif  // XENIA_CPU_BACKEND_A64_A64_OAKNUT_COMPAT_H_

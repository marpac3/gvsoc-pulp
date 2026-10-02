// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// Latency of fdiv.s and fsqrt.s against the operands. A block is 8 identical independent
// instructions, each issued when the previous one returns: cycles = 8 * latency + a constant.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"

#ifdef __riscv_zfinx
#define FA "t3"
#define FB "t4"
#define FC "t5"
#define SET2(b, c) "li " FB ", " #b "\n\tli " FC ", " #c
#define SET1(b)    "li " FB ", " #b
#else
#define FA "ft0"
#define FB "ft1"
#define FC "ft2"
#define SET2(b, c) "li t1, " #b "\n\tfmv.w.x " FB ", t1\n\tli t1, " #c "\n\tfmv.w.x " FC ", t1"
#define SET1(b)    "li t1, " #b "\n\tfmv.w.x " FB ", t1"
#endif

#define DIV8(name, b, c) \
  CALIB_BLOCK(name, 8, SET2(b, c), NORVC(REPT(8, "fdiv.s " FA ", " FB ", " FC)))
#define SQRT8(name, b)   CALIB_BLOCK(name, 8, SET1(b), NORVC(REPT(8, "fsqrt.s " FA ", " FB)))

int main(void)
{
  calib_init();
#ifndef __riscv_zfinx
  // mstatus.FS = Initial: the F instructions are legal.
  __asm__ volatile("li t0, 0x2000\n\tcsrs 0x300, t0" ::: "t0");
#endif

  // fdiv.s: exact quotients with 2 to 24 significant bits, inexact ones, operands that need a
  // normalization, special values.
  DIV8("d_1p2m1_1", 0x3fc00000, 0x3f800000);
  DIV8("d_1p2m2_1", 0x3fa00000, 0x3f800000);
  DIV8("d_1p2m3_1", 0x3f900000, 0x3f800000);
  DIV8("d_1p2m4_1", 0x3f880000, 0x3f800000);
  DIV8("d_1p2m5_1", 0x3f840000, 0x3f800000);
  DIV8("d_1p2m6_1", 0x3f820000, 0x3f800000);
  DIV8("d_1p2m7_1", 0x3f810000, 0x3f800000);
  DIV8("d_1p2m8_1", 0x3f808000, 0x3f800000);
  DIV8("d_1p2m10_1", 0x3f802000, 0x3f800000);
  DIV8("d_1p2m12_1", 0x3f800800, 0x3f800000);
  DIV8("d_1p2m14_1", 0x3f800200, 0x3f800000);
  DIV8("d_1p2m16_1", 0x3f800080, 0x3f800000);
  DIV8("d_1p2m18_1", 0x3f800020, 0x3f800000);
  DIV8("d_1p2m20_1", 0x3f800008, 0x3f800000);
  DIV8("d_1p2m22_1", 0x3f800002, 0x3f800000);
  DIV8("d_1p2m23_1", 0x3f800001, 0x3f800000);
  DIV8("d_1_1", 0x3f800000, 0x3f800000);
  DIV8("d_2_1", 0x40000000, 0x3f800000);
  DIV8("d_1_2", 0x3f800000, 0x40000000);
  DIV8("d_1_3", 0x3f800000, 0x40400000);
  DIV8("d_3_3", 0x40400000, 0x40400000);
  DIV8("d_9_3", 0x41100000, 0x40400000);
  DIV8("d_1p5_1p5", 0x3fc00000, 0x3fc00000);
  DIV8("d_0p75_1p5", 0x3f400000, 0x3fc00000);
  DIV8("d_1_1p5", 0x3f800000, 0x3fc00000);
  DIV8("d_2_1p5", 0x40000000, 0x3fc00000);
  DIV8("d_1p75_1p25", 0x3fe00000, 0x3fa00000);
  DIV8("d_1_7", 0x3f800000, 0x40e00000);
  DIV8("d_7_7", 0x40e00000, 0x40e00000);
  DIV8("d_49_7", 0x42440000, 0x40e00000);
  DIV8("d_1p5_1p25", 0x3fc00000, 0x3fa00000);
  DIV8("d_ffffff_1", 0x3fffffff, 0x3f800000);
  DIV8("d_ffffff_3", 0x3fffffff, 0x40400000);
  DIV8("d_1_ffffff", 0x3f800000, 0x3fffffff);
  DIV8("d_0_3", 0x00000000, 0x40400000);
  DIV8("d_3_0", 0x40400000, 0x00000000);
  DIV8("d_0_0", 0x00000000, 0x00000000);
  DIV8("d_inf_3", 0x7f800000, 0x40400000);
  DIV8("d_3_inf", 0x40400000, 0x7f800000);
  DIV8("d_nan_3", 0x7fc00000, 0x40400000);
  DIV8("d_snan_3", 0x7f800001, 0x40400000);
  DIV8("d_den_3", 0x00000003, 0x40400000);
  DIV8("d_3_den", 0x40400000, 0x00000003);
  DIV8("d_den_den", 0x00400000, 0x00000003);
  DIV8("d_ovf", 0x7f61b1e6, 0x006ce3ee);
  DIV8("d_unf", 0x006ce3ee, 0x7f61b1e6);
  DIV8("d_tiny", 0x00d9c7dd, 0x40400000);
  DIV8("d_neg", 0xbf800000, 0x40400000);
  DIV8("d_negneg", 0xbf800000, 0xc0400000);

  // fsqrt.s: exact roots, inexact ones, special values.
  SQRT8("s_1p0", 0x3f800000);
  SQRT8("s_4p0", 0x40800000);
  SQRT8("s_2p0", 0x40000000);
  SQRT8("s_2p25", 0x40100000);
  SQRT8("s_9p0", 0x41100000);
  SQRT8("s_3p0", 0x40400000);
  SQRT8("s_0p25", 0x3e800000);
  SQRT8("s_0p5", 0x3f000000);
  SQRT8("s_1p5", 0x3fc00000);
  SQRT8("s_1p0009765625", 0x3f802000);
  SQRT8("s_1p000000238418579", 0x3f800002);
  SQRT8("s_6p25", 0x40c80000);
  SQRT8("s_1p5625", 0x3fc80000);
  SQRT8("s_sq1p2m2", 0x3fc80000);
  SQRT8("s_sq1p2m4", 0x3f908000);
  SQRT8("s_sq1p2m8", 0x3f810080);
  SQRT8("s_sq1p2m12", 0x3f801000);
  SQRT8("s_sq1p2m16", 0x3f800100);
  SQRT8("s_sq1p2m20", 0x3f800010);
  SQRT8("s_0", 0x00000000);
  SQRT8("s_m0", 0x80000000);
  SQRT8("s_m1", 0xbf800000);
  SQRT8("s_inf", 0x7f800000);
  SQRT8("s_nan", 0x7fc00000);
  SQRT8("s_den", 0x00000004);
  SQRT8("s_den2", 0x00400000);
  SQRT8("s_max", 0x7f7fffff);

  return 0;
}

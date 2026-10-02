// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// Xpulp timing (COREV_PULP): post-increment and register-register loads and stores, the multiplier,
// immediate branches, bit manipulation, SIMD and hardware loops. 32-bit blocks.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"

#define MUL_PRE "li t1, 3\n\tli t2, 5"
#define BITS4 \
  "cv.addn t0, t1, t2, 2\n\tcv.clip t3, t1, 5\n\tcv.extract t4, t1, 3, 2\n\tcv.cnt t5, t1"

int main(void)
{
  calib_init();

  // Loads and stores with post-increment (cv.lw rd, (rs1), imm) and register-register addressing.
  CALIB_BLOCK("cv_lw_pi", 32, "mv a2, %[buf]", NORVC(REPT(32, "cv.lw t0, (a2), 4")));
  CALIB_BLOCK("cv_lw_pi_use_rd", 32, "mv a2, %[buf]",
              NORVC(REPT(32, "cv.lw t0, (a2), 4\n\taddi t1, t0, 1")));
  CALIB_BLOCK("cv_lw_pi_use_rs1", 32, "mv a2, %[buf]",
              NORVC(REPT(32, "cv.lw t0, (a2), 4\n\taddi t1, a2, 1")));
  CALIB_BLOCK("cv_lw_pi_lw_rs1", 32, "mv a2, %[buf]",
              NORVC(REPT(32, "cv.lw t0, (a2), 4\n\tlw t1, 0(a2)")));
  CALIB_BLOCK("cv_sw_pi", 32, "mv a2, %[buf]", NORVC(REPT(32, "cv.sw t0, (a2), 4")));
  CALIB_BLOCK("cv_lw_rr", 32, "li a3, 4", NORVC(REPT(32, "cv.lw t0, a3(%[buf])")));
  CALIB_BLOCK("cv_lw_rr_use", 32, "li a3, 4",
              NORVC(REPT(32, "cv.lw t0, a3(%[buf])\n\taddi t1, t0, 1")));

  // Multiplier: multiply-accumulate, with the result used next, and the 16-bit forms.
  CALIB_BLOCK("cv_mac", 32, MUL_PRE, NORVC(REPT(32, "cv.mac t0, t1, t2")));
  CALIB_BLOCK("cv_mac_use", 32, MUL_PRE, NORVC(REPT(32, "cv.mac t0, t1, t2\n\taddi t3, t0, 1")));
  CALIB_BLOCK("cv_mulsn", 32, MUL_PRE, NORVC(REPT(32, "cv.mulsn t0, t1, t2, 4")));
  CALIB_BLOCK("cv_mulhhsn", 32, MUL_PRE, NORVC(REPT(32, "cv.mulhhsn t0, t1, t2, 4")));
  CALIB_BLOCK("cv_macsn", 32, MUL_PRE, NORVC(REPT(32, "cv.macsn t0, t1, t2, 4")));
  CALIB_BLOCK("cv_dotsp_h", 32, MUL_PRE, NORVC(REPT(32, "cv.dotsp.h t0, t1, t2")));
  CALIB_BLOCK("cv_sdotsp_h", 32, MUL_PRE, NORVC(REPT(32, "cv.sdotsp.h t0, t1, t2")));

  // Immediate branches, bit manipulation and SIMD ALU.
  CALIB_BLOCK("cv_beqimm_taken", 64, "li t1, 0", NORVC(REPT(64, "cv.beqimm t1, 0, 1f\n1:")));
  CALIB_BLOCK("cv_bneimm_not", 64, "li t1, 0", NORVC(REPT(64, "cv.bneimm t1, 0, 1f\n1:")));
  CALIB_BLOCK("cv_bits", 32, MUL_PRE, NORVC(REPT(8, BITS4)));
  CALIB_BLOCK("cv_add_h", 32, MUL_PRE, NORVC(REPT(32, "cv.add.h t0, t1, t2")));

  // Hardware loops: counts, body sizes, nesting, a load at the end of the body used at its start,
  // and the setup instructions alone. Every body respects the HWLoop constraints of the user manual
  // (corev_hw_loop.rst: no branch, jump or compressed instruction in it).
  CALIB_BLOCK("hwloop16x3", 16, "", NORVC("cv.setupi 0, 16, 2f\n\tnop\n\tnop\n\tnop\n2:"));
  CALIB_BLOCK("hwloop16x4", 16, "", NORVC("cv.setupi 0, 16, 2f\n\tnop\n\tnop\n\tnop\n\tnop\n2:"));
  CALIB_BLOCK("hwloop1x4", 1, "", NORVC("cv.setupi 0, 1, 2f\n\tnop\n\tnop\n\tnop\n\tnop\n2:"));
  CALIB_BLOCK("hwloop2x4", 2, "", NORVC("cv.setupi 0, 2, 2f\n\tnop\n\tnop\n\tnop\n\tnop\n2:"));
  CALIB_BLOCK("hwloop_nested4x4", 4, "",
              NORVC("cv.setupi 1, 4, 4f\n\tcv.setupi 0, 4, 3f\n\t"
                    "nop\n\tnop\n\tnop\n3:\n\tnop\n\tnop\n4:"));
  CALIB_BLOCK("hwloop_lw_wrap", 16, "li t0, 0",
              NORVC("cv.setupi 0, 16, 2f\n\taddi t1, t0, 1\n\tnop\n\tnop\n\tlw t0, 0(%[buf])\n2:"));
  CALIB_BLOCK("cv_counti", 16, "", NORVC(REPT(16, "cv.counti 1, 0")));

  // Events of the hardware loop jumps (jump, branch, taken: 0x380) and of post-increment load-use.
  CALIB_HPM("hwloop16x4", 0x380, "", NORVC("cv.setupi 0, 16, 2f\n\tnop\n\tnop\n\tnop\n\tnop\n2:"));
  CALIB_HPM("hwloop16x4", 0x002, "", NORVC("cv.setupi 0, 16, 2f\n\tnop\n\tnop\n\tnop\n\tnop\n2:"));
  CALIB_HPM("cv_lw_pi_use_rd", 0x004, "mv a2, %[buf]",
            NORVC(REPT(32, "cv.lw t0, (a2), 4\n\taddi t1, t0, 1")));
  CALIB_HPM("cv_lw_pi_use_rs1", 0x004, "mv a2, %[buf]",
            NORVC(REPT(32, "cv.lw t0, (a2), 4\n\taddi t1, a2, 1")));

  return 0;
}

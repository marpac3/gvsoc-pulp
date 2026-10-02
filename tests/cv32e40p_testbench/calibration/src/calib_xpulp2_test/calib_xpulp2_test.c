// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// Xpulp timing (COREV_PULP): the hardware loop jump in DECODE_HWLOOP,
// and in DECODE after a trap, its mret or a status CSR access, where
// the loop end sets the PC like a jump (PC_HWLOOP). 32-bit code.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"

// Trap handler at a 256-byte aligned address (mtvec base), skipping the 4-byte trapping
// instruction, as in calib_int4_test. mtvec is saved in a5 and restored by TRAP_POST.
#define TRAP_PRE  "j 2f\n\t.p2align 8\n3:\n\t" \
                  NORVC("csrr a3, 0x341\n\taddi a3, a3, 4\n\tcsrw 0x341, a3\n\tmret") \
                  "\n2:\n\tcsrr a5, 0x305\n\tla a4, 3b\n\tcsrw 0x305, a4"
#define TRAP_POST "\n\tcsrw 0x305, a5"

// CALIB_HPM with POST run after the counter read.
#define CALIB_HPM_POST(name, event, pre, body, post)                                       \
  do {                                                                                     \
    uint32_t n_;                                                                           \
    __asm__ volatile("csrw 0x323, %[ev]\n\t"                                               \
                     pre "\n\t"                                                            \
                     ".p2align 4\n\t"                                                      \
                     "csrw 0xB03, x0\n\t"                                                  \
                     body "\n\t"                                                           \
                     "csrr %[n], 0xB03\n\t"                                                \
                     post                                                                  \
                     : [n] "=&r"(n_)                                                       \
                     : [buf] "r"(calib_buf), [ev] "r"(event)                               \
                     : CALIB_CLOBBERS);                                                    \
    printf("CALIB_HPM %s event=0x%x count=%u\n", name, (unsigned)(event), (unsigned)n_);   \
  } while (0)

int main(void)
{
  calib_init();

  // An ecall in the body. The mret returns to the loop end (a jump
  // from ID in DECODE), to the instruction before it (DECODE kept for
  // it, then the loop end), or earlier (DECODE_HWLOOP again).
  CALIB_BLOCK_POST("hwlp_ecall_end8", 8, TRAP_PRE,
                   NORVC("cv.setupi 0, 8, 2f\n\tnop\n\tecall\n\tnop\n2:"), TRAP_POST);
  CALIB_BLOCK_POST("hwlp_ecall_penult8", 8, TRAP_PRE,
                   NORVC("cv.setupi 0, 8, 2f\n\tnop\n\tecall\n\tnop\n\tnop\n2:"), TRAP_POST);
  CALIB_BLOCK_POST("hwlp_ecall_mid8", 8, TRAP_PRE,
                   NORVC("cv.setupi 0, 8, 2f\n\tecall\n\tnop\n\tnop\n\tnop\n2:"), TRAP_POST);
  CALIB_BLOCK_POST("hwlp_ecall_end2", 2, TRAP_PRE,
                   NORVC("cv.setupi 0, 2, 2f\n\tnop\n\tecall\n\tnop\n2:"), TRAP_POST);
  // A status CSR access (mepc read) in the body: the flush ends in DECODE.
  CALIB_BLOCK("hwlp_csr_second8", 8, "",
              NORVC("cv.setupi 0, 8, 2f\n\tnop\n\tcsrr t1, 0x341\n\tnop\n\tnop\n2:"));
  CALIB_BLOCK("hwlp_csr_first8", 8, "",
              NORVC("cv.setupi 0, 8, 2f\n\tcsrr t1, 0x341\n\tnop\n\tnop\n\tnop\n2:"));
  CALIB_BLOCK("hwlp_csr_penult8", 8, "",
              NORVC("cv.setupi 0, 8, 2f\n\tnop\n\tnop\n\tcsrr t1, 0x341\n\tnop\n2:"));

  // The jump set from ID is not a jump event (jump, branch, taken: 0x380).
  CALIB_HPM_POST("hwlp_ecall_end8", 0x380, TRAP_PRE,
                 NORVC("cv.setupi 0, 8, 2f\n\tnop\n\tecall\n\tnop\n2:"), TRAP_POST);

  return 0;
}

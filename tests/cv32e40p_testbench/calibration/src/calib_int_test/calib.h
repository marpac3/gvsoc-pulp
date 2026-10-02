// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// Timing helpers of the calibration programs. A block runs between two reads of mcycle and prints
//   CALIB <name> cycles=<n> iters=<k>
// CALIB_HPM runs the block again with mhpmevent3 on one event and prints the mhpmcounter3 delta.
// The same ELF runs on the RTL, without memory stalls, and on the model.

#pragma once

#include <stdio.h>
#include <stdint.h>

#define HPM_LD_STALL  (1u << 2)
#define HPM_JR_STALL  (1u << 3)
#define HPM_IMISS     (1u << 4)

static volatile uint32_t calib_buf[64] __attribute__((aligned(16)));

static inline void calib_init(void)
{
#ifdef CALIB_FAST
  // Every counter but mcycle inhibited: the model runs its fast handlers (Cv32e40pExec), which must
  // give the same timing. The RTL timing does not depend on mcountinhibit.
  __asm__ volatile("csrw 0x320, %0" : : "r"(~1u));
#else
  // All counters on (mcountinhibit resets to inhibit them).
  __asm__ volatile("csrw 0x320, x0");
#endif
  for (int i = 0; i < 64; i++) calib_buf[i] = i;
}

#define CALIB_CLOBBERS "memory", "ra", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a2", "a3", "a4", \
                       "a5"

// PRE runs before the first mcycle read (operand setup), BODY is timed. %[buf] holds calib_buf.
#define CALIB_BLOCK(name, iters, pre, body)                                                \
  do {                                                                                     \
    uint32_t s_, e_;                                                                       \
    __asm__ volatile(pre "\n\t"                                                            \
                     ".p2align 4\n\t"                                                      \
                     "csrr %[s], 0xB00\n\t"                                                \
                     body "\n\t"                                                           \
                     "csrr %[e], 0xB00"                                                    \
                     : [s] "=&r"(s_), [e] "=&r"(e_)                                        \
                     : [buf] "r"(calib_buf)                                                \
                     : CALIB_CLOBBERS);                                                    \
    printf("CALIB %s cycles=%u iters=%u\n", name, (unsigned)(e_ - s_), (unsigned)(iters)); \
  } while (0)

// CALIB_BLOCK with POST run after the second mcycle read, to restore what PRE changed (mtvec).
#define CALIB_BLOCK_POST(name, iters, pre, body, post)                                     \
  do {                                                                                     \
    uint32_t s_, e_;                                                                       \
    __asm__ volatile(pre "\n\t"                                                            \
                     ".p2align 4\n\t"                                                      \
                     "csrr %[s], 0xB00\n\t"                                                \
                     body "\n\t"                                                           \
                     "csrr %[e], 0xB00\n\t"                                                \
                     post                                                                  \
                     : [s] "=&r"(s_), [e] "=&r"(e_)                                        \
                     : [buf] "r"(calib_buf)                                                \
                     : CALIB_CLOBBERS);                                                    \
    printf("CALIB %s cycles=%u iters=%u\n", name, (unsigned)(e_ - s_), (unsigned)(iters)); \
  } while (0)

#define CALIB_HPM(name, event, pre, body)                                                  \
  do {                                                                                     \
    uint32_t n_;                                                                           \
    __asm__ volatile("csrw 0x323, %[ev]\n\t"                                               \
                     pre "\n\t"                                                            \
                     ".p2align 4\n\t"                                                      \
                     "csrw 0xB03, x0\n\t"                                                  \
                     body "\n\t"                                                           \
                     "csrr %[n], 0xB03"                                                    \
                     : [n] "=&r"(n_)                                                       \
                     : [buf] "r"(calib_buf), [ev] "r"(event)                               \
                     : CALIB_CLOBBERS);                                                    \
    printf("CALIB_HPM %s event=0x%x count=%u\n", name, (unsigned)(event), (unsigned)n_);   \
  } while (0)

// BODY leaves a value in %[v], such as a counter read right after its write, and prints
//   CALIB_READ <name> value=<v>
#define CALIB_READ(name, pre, body)                                                        \
  do {                                                                                     \
    uint32_t v_;                                                                           \
    __asm__ volatile(pre "\n\t"                                                            \
                     ".p2align 4\n\t"                                                      \
                     body                                                                  \
                     : [v] "=&r"(v_)                                                       \
                     : [buf] "r"(calib_buf)                                                \
                     : CALIB_CLOBBERS);                                                    \
    printf("CALIB_READ %s value=%u\n", name, (unsigned)v_);                                \
  } while (0)

#ifdef CALIB_FAST
// The event counters are inhibited.
#undef CALIB_HPM
#define CALIB_HPM(name, event, pre, body) do { } while (0)
#endif

#define REPT(n, s) ".rept " #n "\n\t" s "\n\t.endr"

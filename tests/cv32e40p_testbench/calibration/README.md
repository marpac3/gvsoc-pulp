# CV32E40P timing calibration

These programs time instruction blocks on the `cv32e40p_testbench` target and compare them with
the RTL. Each program prints one line per block:

- `CALIB <block> cycles=<n> iters=<n>`: `mcycle` across the block;
- `CALIB_HPM <block> event=<mask> count=<n>`: `mhpmcounter3` across the block run again, with
  `mhpmevent3` set to `<mask>`;
- `CALIB_READ <block> value=<n>`: a counter as read by the block.

`refs/<program>_<configuration>.txt` holds the lines printed by the same binary on the RTL, in the
CV32E40P UVM testbench of [core-v-verif](https://github.com/openhwgroup/core-v-verif) without bus
stalls (`+rand_stall_obi_disable`), RTL of cv32e40p `dd1f8e4` (`cv32e40p_v1.8.3` and two fixes).
Every line is a bench of the test, with tolerance 0: the test fails when a block takes one cycle
more or less than on the RTL.

```
gvtest --target cv32e40p_testbench run
```

| Program | Times | Configurations |
|---|---|---|
| `calib_int` | RV32IMC: ALU, branches, jumps, loads and stores, multiplier, divider, CSRs, fences | `default` |
| `calib_int2` | the hazards and the traps `calib_int` leaves open | `default` |
| `calib_int3` | the counters read right after a write, the cycle event, `mcountinhibit` | `default` |
| `calib_int4` | the other illegal instructions, the fetch after a return or a fence, the JALR hazards | `default` |
| `calib_int5` | a jump in ID behind a multi-cycle instruction in EX, the load stall | `default` |
| `calib_int6` | the bubble behind a JALR, the load stall on an ALU write | `default` |
| `calib_int_fast`, `calib_int2_fast` | `calib_int`, `calib_int2` with every counter but `mcycle` inhibited | `default` |
| `calib_xpulp` | Xpulp: post-increment accesses, MAC, immediate branches, bit manipulation, SIMD, hardware loops | `pulp` |
| `calib_xpulp2` | the hardware loop jump in DECODE and DECODE_HWLOOP | `pulp` |
| `calib_fp` | the F instructions, the FP loads and stores, the FP CSRs | FPU |
| `calib_fp2` | the APU dispatcher, the shared write port, the CSR stall behind the APU | FPU |
| `calib_fp3` | the divider result kept in EX, the APU events of the counters | FPU |
| `calib_fp4` | the load stall on an FP write | FPU |
| `calib_fp5` | the multi-cycle results kept in EX, the dispatcher on the fences and `ebreak` | FPU |
| `calib_fpdiv` | `fdiv.s` and `fsqrt.s` against the operands | `pulp_fpu`, `pulp_fpu_zfinx` |

FPU: `pulp_fpu`, `pulp_fpu_zfinx` and their `_1cyclat` and `_2cyclat` variants, where the FPU
latencies (`FPU_ADDMUL_LAT`, `FPU_OTHERS_LAT`) are 1 and 2. A variant runs the binary of its base
configuration, with `fpu_addmul_lat` and `fpu_others_lat` set on the target.

The programs were built by the core-v-verif makefiles (`make test TEST=<program>_test
CFG=<configuration>`) with the CORE-V GCC 14.1.0 toolchain (`corev-openhw-gcc-modded-v0.1`).
`src/` holds their sources, laid out as in `cv32e40p/tests/programs/custom/` of core-v-verif.

# MIPS Circuit Model

## Scope

This is a functional CPU circuit model, not a transistor-level reconstruction.

## Major blocks

1. 32-entry general-register file.
2. Constant-zero enforcement for R0.
3. Program counter.
4. Instruction fetch/decode.
5. Immediate/sign/zero extension.
6. ALU and comparator.
7. Shifter.
8. Multiply/divide path with HI/LO.
9. Pipeline stage registers.
10. Load/store address and data paths.
11. Branch/jump control.
12. CP0/system-control path.
13. Exception/interrupt control.
14. External bus interface.
15. Clock/control logic.

## Couplers

SLCoupler models:

- register file -> ALU;
- ALU -> register file;
- register file -> address generation;
- memory -> writeback;
- branch comparator -> PC control;
- CP0 -> exception control;
- pipeline stage -> pipeline stage.

## Accuracy boundary

No particular cache, TLB, FPU, bus protocol, pipeline depth beyond the functional abstraction, or transistor layout is claimed unless supported by a concrete MIPS implementation source.

## Executable circuit model

`SLMIPSCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLMIPSCPU.circuitModel()`
returns it built and clocked.

The class header marks every figure as recalled or derived. Recalled figures
come from general knowledge and still need checking against a primary source.
Derived figures are planning numbers, not measurements. The model is at
functional-block level, not a transistor netlist.

Circuit grade: **B** (7/10), with 0 verification violations, from
`circuitScore()` in `SLCircuitModel`. `lib/cpu/examples/circuit-grades.sleela`
grades every CPU.

## Power delivery

The circuit model carries an `SLPowerNetwork` (series R and L, decoupling C, load) on
`SLCircuitDetails`. Its values are derived planning figures named in the class. On a soft-started
power-on the die rail settles at 4950 mV with 14408 uV ripple after the ramp, a 556 mA peak
supply current and a 8130 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: R3000; caches off chip; R3010 FPU external; on-chip TLB. 1 core(s), 1 thread(s) per core,
pipelined, 5 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU external, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 21282 ps; named blocks explain 57.3 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

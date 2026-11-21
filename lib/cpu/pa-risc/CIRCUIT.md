# PA-RISC Circuit Model

## Scope

Functional architecture/microarchitecture model, not transistor reconstruction.

## Major blocks

1. Instruction fetch.
2. Decoder.
3. General register file.
4. Shadow/fast-interrupt state.
5. Integer ALU.
6. Shift/bit-manipulation unit.
7. Branch/control unit.
8. Load/store unit.
9. Floating-point unit.
10. MAX multimedia unit where enabled.
11. Issue/dependency/reorder structures.
12. MMU/TLB.
13. Cache hierarchy.
14. Trap/interrupt controller.
15. System interconnect.
16. Clock/control.

## Couplers

SLCoupler models:

- decoder -> register file;
- registers -> ALU;
- ALU -> register file;
- branch -> PC;
- load/store -> MMU;
- MMU -> TLB/cache;
- FPU/MAX -> register state;
- dependency unit -> execution units;
- reorder -> retirement;
- trap -> control state;
- cache -> interconnect;
- DMA -> bus arbitration.

## Accuracy boundary

No specific PA-7000/PA-8000/PA-8900 pipeline width, cache geometry, transistor topology, or execution-port arrangement is assigned to generic PA-RISC.

## Executable circuit model

`SLPARISCCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLPARISCCPU.circuitModel()`
returns it built and clocked.

The class header marks every figure as recalled or derived. Recalled figures
come from general knowledge and still need checking against a primary source.
Derived figures are planning numbers, not measurements. The model is at
functional-block level, not a transistor netlist.

Circuit grade: **C** (6/10), with 0 verification violations, from
`circuitScore()` in `SLCircuitModel`. `lib/cpu/examples/circuit-grades.sleela`
grades every CPU.

## Power delivery

The circuit model carries an `SLPowerNetwork` (series R and L, decoupling C, load) on
`SLCircuitDetails`. Its values are derived planning figures named in the class. On a soft-started
power-on the die rail settles at 4940 mV with 14702 uV ripple after the ramp, a 3239 mA peak
supply current and a 18357 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: PA-7100 class; caches off chip. 1 core(s), 1 thread(s) per core,
superscalar in order, 5 pipeline stage(s), issue width 2. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

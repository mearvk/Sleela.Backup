# SPARC Circuit Model

## Scope

Functional architectural/microarchitectural model, not transistor-level reconstruction.

## Major blocks

1. Instruction fetch.
2. Decoder.
3. Register-window file.
4. Integer unit/ALU.
5. PC/nPC control.
6. Branch/control unit.
7. Load/store address generation.
8. Floating-point unit.
9. Trap/privilege controller.
10. Window spill/fill controller.
11. MMU/TLB.
12. Cache hierarchy.
13. System interconnect.
14. Clock/control.

## Couplers

SLCoupler models:

- decoder -> window register file;
- window registers -> integer unit;
- integer unit -> window registers;
- branch -> PC/nPC;
- load/store -> MMU;
- MMU -> TLB/cache;
- SAVE/RESTORE -> window controller;
- window controller -> trap controller;
- FPU -> register state;
- trap -> privileged state;
- cache -> interconnect;
- DMA -> bus arbitration.

## Accuracy boundary

No specific SPARC implementation's transistor topology, pipeline width, cache geometry, branch predictor, or execution-port count is asserted.

## Executable circuit model

`SLSparcCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLSparcCPU.circuitModel()`
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
power-on the die rail settles at 4950 mV with 14408 uV ripple after the ramp, a 556 mA peak
supply current and a 8130 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: SPARC V8 class; implementation not stated. 1 core(s), 1 thread(s) per core,
pipelined, 4 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

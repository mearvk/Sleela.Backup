# RISC-V Circuit Model

## Scope

Functional architectural and microarchitectural model, not a transistor reconstruction.

## Major blocks

1. Register file.
2. Program counter.
3. Instruction fetch.
4. Decoder.
5. Immediate-generation logic.
6. Integer ALU.
7. Branch/control unit.
8. Load/store address generation.
9. Multiply/divide unit where M is enabled.
10. Atomic unit where A is enabled.
11. FP unit where F/D is enabled.
12. Vector unit where V is enabled.
13. CSR/system-control block.
14. MMU/TLB and page-table walk.
15. Cache hierarchy.
16. Interrupt/exception control.
17. Interconnect interface.
18. Clock/control.

## Couplers

SLCoupler models:

- decoder -> register file;
- register file -> ALU;
- immediate -> ALU;
- ALU -> register file;
- branch -> PC;
- load/store -> MMU;
- MMU -> TLB/cache;
- CSR -> privilege/translation state;
- extension unit -> register file;
- cache -> interconnect;
- DMA -> bus arbitration;
- exception -> trap state.

## Accuracy boundary

No particular RISC-V vendor's pipeline depth, cache geometry, branch predictor, execution-port count, or physical circuit layout is asserted by this generic implementation.

## Executable circuit model

`SLRISCVCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLRISCVCPU.circuitModel()`
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

Recalled, not checked against a source: RV32I five-stage reference pipeline. 1 core(s), 1 thread(s) per core,
pipelined, 5 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

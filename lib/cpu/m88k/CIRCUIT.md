# Motorola 88000 Circuit Model

## Scope

Functional architectural/microarchitectural model, not transistor reconstruction.

## Major blocks

1. Instruction fetch.
2. Decoder.
3. General register file.
4. Control-register block.
5. Integer ALU.
6. Shift/multiply/divide units.
7. Branch/control unit.
8. Floating-point execution.
9. Load/store unit.
10. Scoreboard/issue logic.
11. MMU/TLB.
12. Cache hierarchy.
13. Trap/interrupt controller.
14. System interconnect.
15. Clock/control.

## Couplers

SLCoupler models:

- decoder -> register file;
- registers -> integer/FP units;
- branch -> PC/control;
- load/store -> MMU;
- MMU -> TLB/cache;
- scoreboard -> execution units;
- control registers -> MMU/interrupt state;
- cache -> interconnect;
- DMA -> arbitration;
- exception -> processor state.

## Accuracy boundary

No MC88100/MC88110 transistor topology, cache geometry, or exact pipeline timing is claimed by generic m88k.

## Executable circuit model

`SLM88KCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLM88KCPU.circuitModel()`
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

Recalled, not checked against a source: MC88100; caches and MMU in the external MC88200 CMMU. 1 core(s), 1 thread(s) per core,
pipelined, 3 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); named blocks explain 42.8 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

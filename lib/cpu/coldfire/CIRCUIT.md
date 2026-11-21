# Motorola ColdFire Circuit Model

## Functional scope

Architectural/functional model, not transistor reconstruction.

## Major blocks

1. Instruction fetch.
2. Decoder.
3. Register file.
4. Effective-address unit.
5. Integer execution.
6. Optional MAC/DSP.
7. Optional FPU.
8. Optional MMU/TLB.
9. Optional I/D cache.
10. Exception/interrupt control.
11. Debug/trace interface.
12. Embedded bus interface.
13. Clock/pipeline control.

## Couplers

SLCoupler models:

- fetch -> decoder;
- decoder -> register file;
- registers -> execution units;
- address generation -> MMU/cache;
- cache -> bus;
- MAC/FPU -> completion;
- exception -> processor state;
- DMA -> arbitration.

## Accuracy boundary

ColdFire V1-V4e are not collapsed into one invented microarchitecture.

## Executable circuit model

`SLColdFireCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLColdFireCPU.circuitModel()`
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
power-on the die rail settles at 3241 mV with 9416 uV ripple after the ramp, a 3125 mA peak
supply current and a 18180 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: representative V4 class; implementation not stated. 1 core(s), 1 thread(s) per core,
pipelined, 5 pipeline stage(s), issue width 1. Caches: L1I 8 KB, L1D 8 KB,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

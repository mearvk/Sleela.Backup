# DEC VAX Circuit Model

## Functional scope

This is an architectural/functional circuit model, not transistor reconstruction.

## Major blocks

1. Instruction fetch.
2. Variable-length decoder.
3. Operand/address generator.
4. Register file.
5. Integer execution.
6. Floating-point execution.
7. String/character execution.
8. Memory-management unit.
9. Cache interface where implemented.
10. Exception/interrupt control.
11. Bus interface.
12. Clock and sequencing.

## Couplers

SLCoupler models:

- fetch -> decoder;
- decoder -> operand generator;
- registers -> execution units;
- operand generator -> memory management;
- memory/cache -> bus;
- execution -> condition state;
- exception controller -> processor state;
- DMA -> bus arbitration.

## Accuracy boundary

VAX generations are represented as profiles rather than one invented microarchitecture.

## Executable circuit model

`SLVAXCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLVAXCPU.circuitModel()`
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
power-on the die rail settles at 4950 mV with 16412 uV ripple after the ramp, a 5225 mA peak
supply current and a 185890 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: VAX-11/780 with 8 KB cache. 1 core(s), 1 thread(s) per core,
microcoded, 1 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 8 KB per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

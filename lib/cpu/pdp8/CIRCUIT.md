# PDP-8 Logical Circuit Abstraction

## Scope

This is a logical educational/simulation abstraction, not a transistor-level DEC PDP-8 schematic.

## Functional blocks

- 12-bit instruction fetch and decode.
- 12-bit accumulator and separate link bit.
- 12-bit program counter and address selection.
- Memory-reference effective-address logic.
- Operate-instruction micro-operation control.
- Optional EAE arithmetic block.
- IOT dispatch and interrupt interface.
- Word-addressed memory interface.

## Variant control

Add memory-extension logic, arithmetic options, and device interfaces only when documented for the selected model. Do not infer gate counts, physical layout, power draw, or clock speed.

## Executable circuit model

`SLPDP8Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLPDP8CPU.circuitModel()`
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
power-on the die rail settles at -2950 mV with 9347 uV ripple after the ramp, a 5080 mA peak
supply current and a 183846 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: single core, no caches, no FPU, no on-chip MMU. 1 core(s), 1 thread(s) per core,
sequential, 1 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

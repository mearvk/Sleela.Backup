# z/Architecture Logical Circuit Abstraction

## Scope

This document describes logical blocks for simulation and education; it is not a physical IBM processor netlist.

## Functional blocks

- Versioned instruction fetch/decode and execution control.
- General-purpose register file and condition-code state.
- PSW and privileged control-state management.
- Arithmetic, branch, and storage-operation units.
- Optional floating-point, vector, decimal and other facility blocks.
- Translation/protection interface to the system model.
- Interruption interface and optional performance abstraction.

## System boundary

Main storage controllers, cache hierarchy details, channel subsystem, I/O devices, multiprocessor interconnect and firmware are system-level components unless the selected target model explicitly integrates a defined part.

## Validation

Instantiate only facilities enabled for the selected profile. Do not infer transistor count, circuit topology, clock rate or power consumption from this logical model.

## Executable circuit model

`SLZArchitectureCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLZArchitectureCPU.circuitModel()`
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
power-on the die rail settles at 1451 mV with 3316 uV ripple after the ramp, a 10235 mA peak
supply current and a 25434 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: z900 class at 770 MHz; millicode. 20 core(s), 1 thread(s) per core,
pipelined, 5 pipeline stage(s), issue width 1. Caches: L1I 256 KB, L1D 256 KB,
L1 unified 0 per core; L2 32 MB, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

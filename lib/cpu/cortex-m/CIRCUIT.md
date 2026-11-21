# Cortex-M Circuit-Level Abstraction

## Scope

This document defines a logical digital abstraction for simulation and teaching. It is not a transistor-level netlist or a claim about a specific vendor's silicon.

## Functional blocks

- Thumb instruction fetch/decode and execution control.
- General-purpose register bank and status/exception state.
- Arithmetic/logic and optional DSP/FPU/MVE units.
- Exception interface and NVIC-facing signals.
- Configurable memory interface, optional MPU and cache blocks.
- Optional debug/trace, security attribution, and implementation-defined blocks.

## Signal-level concepts

Represent clock/reset, instruction/data request and response, exception/interrupt inputs, fault outputs, and bus handshakes at the abstraction level needed by the simulator. Exact signal names and timing are implementation-specific.

## Optionality

FPU, DSP, MVE, TrustZone-M, MPU, caches, and debug/trace must be conditionally instantiated. DMA and most peripherals belong to the SoC diagram, not the core schematic.

## Validation

Circuit abstraction must remain consistent with the selected architecture profile. Do not infer gate count, transistor count, physical timing, or power figures without a documented implementation source.

## Executable circuit model

`SLCortexMCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLCortexMCPU.circuitModel()`
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

Recalled, not checked against a source: Cortex-M3 or M4 class; MPU only. 1 core(s), 1 thread(s) per core,
pipelined, 3 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

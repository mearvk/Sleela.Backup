# Cortex-R Circuit-Level Abstraction

## Scope

This is a logical block-level representation for SLeeLa simulation and teaching, not a transistor-level netlist.

## Functional blocks

- Profile-specific instruction fetch, decode and execution.
- Register banks and status/control state.
- Arithmetic/logic and optional FPU/DSP units.
- Exception and interrupt interface.
- Memory protection/translation block where implemented.
- Optional cache and TCM interfaces.
- Bus and memory-system interfaces.
- Optional debug/trace, virtualization and safety-related interfaces.

## Signal groups

At the abstraction level needed by the simulator, represent clock/reset, instruction/data requests and responses, interrupt inputs, exception/fault outputs, memory attributes, and barrier/order control. Exact signal names and cycle timing are implementation-specific.

## Optional blocks

Do not instantiate caches, TCM, FPU, MPU/MMU, virtualization, or safety monitors without target evidence. External DMA and peripherals belong in the SoC-level circuit.

## Validation

Ensure every block corresponds to the selected profile and configuration. Do not infer transistor counts, physical timing, power, or gate-level topology without a documented implementation source.

## Executable circuit model

`SLCortexRCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLCortexRCPU.circuitModel()`
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

Recalled, not checked against a source: Cortex-R5 class; MPU only. 1 core(s), 1 thread(s) per core,
superscalar in order, 8 pipeline stage(s), issue width 2. Caches: L1I 32 KB, L1D 32 KB,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

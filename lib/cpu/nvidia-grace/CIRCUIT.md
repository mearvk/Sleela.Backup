# NVIDIA Grace (ARM Neoverse V2) Circuit Model

## Scope

This document describes the architectural circuit blocks SLeeLa models reliably for the NVIDIA Grace (ARM Neoverse V2). It is not a transistor-for-transistor recreation.

## Major blocks

1. **Register section**
   - AArch64 register model; many Neoverse-V2 cores with a coherent mesh and large shared cache

2. **ALU**
   - arithmetic;
   - logic;
   - shifts/rotates;
   - flag generation.

3. **Address path**
   - 48-bit address formation;
   - address latches;
   - effective-address computation.

4. **Data path**
   - 64-bit data latch;
   - internal data movement;
   - external data bus buffer.

5. **Control**
   - instruction register;
   - instruction decode;
   - timing control;
   - interrupt logic.

6. **Clock**
   - clock input;
   - phase sequencing.

## Accuracy boundary

Gate-level or transistor-level detail should be added only from sufficiently reliable die analysis or a validated hardware recreation. This profile stays at the architectural/behavioral/timing level.

## Sources

- https://www.nvidia.com/en-us/data-center/grace-cpu/
- https://en.wikipedia.org/wiki/ARM_Neoverse

## Executable circuit model

`SLNvidiaGraceCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLNvidiaGraceCPU.circuitModel()`
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
power-on the die rail settles at 775 mV with 1781 uV ripple after the ramp, a 51474 mA peak
supply current and a 25434 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: 72 Neoverse V2 cores. 72 core(s), 1 thread(s) per core,
out of order, 10 pipeline stage(s), issue width 8. Caches: L1I 64 KB, L1D 64 KB,
L1 unified 0 per core; L2 72 MB, L3 114 MB per chip; local store 0.
FPU on chip, MMU paged, SIMD 128 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 210 ps; transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

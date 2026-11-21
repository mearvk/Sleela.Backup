# Xbox One (x86-64 Jaguar) Circuit Model

## Scope

This document describes the architectural circuit blocks SLeeLa models reliably for the Xbox One (x86-64 Jaguar). It is not a transistor-for-transistor recreation.

## Major blocks

1. **Register section**
   - x86-64 register model; two quad-core AMD Jaguar modules (8 cores)

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

- https://en.wikipedia.org/wiki/Xbox_One
- https://en.wikipedia.org/wiki/Jaguar_(microarchitecture)

## Executable circuit model

`SLXboxOneCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLXboxOneCPU.circuitModel()`
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
power-on the die rail settles at 1075 mV with 2522 uV ripple after the ramp, a 53071 mA peak
supply current and a 25434 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: eight Jaguar cores. 8 core(s), 1 thread(s) per core,
out of order, 14 pipeline stage(s), issue width 2. Caches: L1I 32 KB, L1D 32 KB,
L1 unified 0 per core; L2 4 MB, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 128 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 461 ps; named blocks explain 5.3 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

## On-die GPU

Recalled, not checked against a source: GCN, 12 CUs at 853 MHz. `setGpu` and `buildGpu()` add the lanes (25000
transistors per 32-bit lane), register files and local memory per unit, the GPU L2, control
(2 million transistors per unit), a shader-stage timing path at the GPU clock, and a
unified-memory semaphore that the CPU and GPU take in turn. The named blocks, GPU included, now explain 48.2 percent of the known transistor total. Texture units, render
back ends, media engines, neural engines, PHYs and other I/O are not modelled.

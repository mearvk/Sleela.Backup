# Intel Xeon (x86-64) Circuit Model

## Scope

This document describes the architectural circuit blocks SLeeLa models reliably for the Intel Xeon (x86-64). It is not a transistor-for-transistor recreation.

## Major blocks

1. **Register section**
   - x86-64 register model with large shared L3, more cores and ECC/RAS features

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

- https://en.wikipedia.org/wiki/Xeon
- https://en.wikipedia.org/wiki/X86-64

## Executable circuit model

`SLIntelXeonCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLIntelXeonCPU.circuitModel()`
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
power-on the die rail settles at 975 mV with 2275 uV ripple after the ramp, a 52565 mA peak
supply current and a 25434 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: representative Skylake-SP 28 cores; generation not stated. 28 core(s), 2 thread(s) per core,
out of order, 14 pipeline stage(s), issue width 4. Caches: L1I 32 KB, L1D 32 KB,
L1 unified 0 per core; L2 28 MB, L3 39424 KB per chip; local store 0.
FPU on chip, MMU paged, SIMD 512 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

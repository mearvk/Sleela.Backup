# PlayStation 2 (Emotion Engine / MIPS) Circuit Model

## Scope

This document describes the architectural circuit blocks SLeeLa models reliably for the PlayStation 2 (Emotion Engine / MIPS). It is not a transistor-for-transistor recreation.

## Major blocks

1. **Register section**
   - MIPS III/IV register model (32 128-bit GP, r0 zero) in the Emotion Engine

2. **ALU**
   - arithmetic;
   - logic;
   - shifts/rotates;
   - flag generation.

3. **Address path**
   - 32-bit address formation;
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

- https://en.wikipedia.org/wiki/Emotion_Engine
- https://en.wikipedia.org/wiki/Comparison_of_PlayStation_consoles

## Executable circuit model

`SLPlayStation2Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLPlayStation2CPU.circuitModel()`
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
power-on the die rail settles at 1751 mV with 4056 uV ripple after the ramp, a 10407 mA peak
supply current and a 25434 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: R5900 with 128-bit MMI; 16 KB scratchpad; VU0 and VU1 not modelled. 1 core(s), 1 thread(s) per core,
superscalar in order, 6 pipeline stage(s), issue width 2. Caches: L1I 16 KB, L1D 8 KB,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 16 KB.
FPU on chip, MMU paged, SIMD 128 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 1944 ps; named blocks explain 27.1 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

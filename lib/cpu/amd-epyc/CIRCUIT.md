# AMD EPYC (Zen x86-64) Circuit Model

## Scope

This document describes the architectural circuit blocks SLeeLa models reliably for the AMD EPYC (Zen x86-64). It is not a transistor-for-transistor recreation.

## Major blocks

1. **Register section**
   - x86-64 register model (Zen) with chiplet CCDs, large shared L3 and ECC/RAS

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

- https://en.wikipedia.org/wiki/Epyc
- https://en.wikipedia.org/wiki/Zen_(microarchitecture)

## Executable circuit model

`SLAMDEPYCCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLAMDEPYCCPU.circuitModel()`
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

Recalled, not checked against a source: Zen 4 class, 96 cores. 96 core(s), 2 thread(s) per core,
out of order, 19 pipeline stage(s), issue width 6. Caches: L1I 32 KB, L1D 32 KB,
L1 unified 0 per core; L2 96 MB, L3 384 MB per chip; local store 0.
FPU on chip, MMU paged, SIMD 512 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 211 ps; transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

## SKU circuit models

Each SKU entry has its own circuit (`SL<SKU>Circuit`, extending the family circuit) and its CPU class
returns it from `circuitModel()`. Cores, threads, L3, TDP and clock come from this folder
SPECIFICATION.md and the SKU class; the Vcore rail (1200 mV, 950 to 1450 mV) from
`SLAMDThreadripperPlatform`; the load current is TDP divided by 1.2 V; the rest is recalled or derived
as each class header says.

| SKU | Grade | Cores | Cache | Settled Vcore | Peak current | Stage slack | Violations | CPU link |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| AMD EPYC 9965 (Zen 5c Turin, 192C/384T) | C (6/10) | 192 | 591 MB | 1187 mV | 433 A | 211 ps | 0 | 0 |
| AMD EPYC 9755 (Zen 5 Turin, 128C/256T) | C (6/10) | 128 | 650 MB | 1187 mV | 433 A | 186 ps | 0 | 0 |
| AMD EPYC 9645 (Zen 5c Turin, 96C/192T) | C (6/10) | 96 | 359 MB | 1190 mV | 352 A | 211 ps | 0 | 0 |
| AMD EPYC 9575F (Zen 5 Turin, 64C/128T, 5.0 GHz) | C (6/10) | 64 | 325 MB | 1190 mV | 352 A | 145 ps | 0 | 0 |
| AMD EPYC 9475F (Zen 5 Turin, 48C/96T, 4.8 GHz) | C (6/10) | 48 | 307 MB | 1190 mV | 352 A | 153 ps | 0 | 0 |
| AMD EPYC 9355 (Zen 5 Turin, 32C/64T) | C (6/10) | 32 | 290 MB | 1193 mV | 253 A | 171 ps | 0 | 0 |
| AMD EPYC 9175F (Zen 5 Turin, 16C/32T, 5.0 GHz) | C (6/10) | 16 | 529 MB | 1192 mV | 286 A | 145 ps | 0 | 0 |

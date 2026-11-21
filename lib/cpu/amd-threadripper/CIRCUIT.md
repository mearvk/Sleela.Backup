# AMD Ryzen Threadripper 9000 (Zen 5 x86-64) Circuit Model

## Scope

This document describes the architectural circuit blocks SLeeLa models reliably for the AMD Ryzen Threadripper 9000 (Zen 5 x86-64). It is not a transistor-for-transistor recreation.

## Major blocks

1. **Register section**
   - x86-64 register model (Zen 5 microarchitecture) with per-core L1/L2 and shared L3 per CCD

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

- https://en.wikipedia.org/wiki/Zen_5
- https://en.wikipedia.org/wiki/Ryzen_Threadripper
- https://en.wikipedia.org/wiki/List_of_AMD_Ryzen_processors

## Executable circuit model

`SLAMDThreadripperCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLAMDThreadripperCPU.circuitModel()`
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

Recalled, not checked against a source: Zen 5 class, 64 cores. 64 core(s), 2 thread(s) per core,
out of order, 19 pipeline stage(s), issue width 8. Caches: L1I 32 KB, L1D 48 KB,
L1 unified 0 per core; L2 64 MB, L3 256 MB per chip; local store 0.
FPU on chip, MMU paged, SIMD 512 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 132 ps; transistor total unknown, so no budget check.

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
| AMD Ryzen Threadripper 9960X (Zen 5, 24C/48T) | C (6/10) | 24 | 153 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |
| AMD Ryzen Threadripper 9970X (Zen 5, 32C/64T) | C (6/10) | 32 | 162 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |
| AMD Ryzen Threadripper 9980X (Zen 5, 64C/128T) | C (6/10) | 64 | 325 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |
| AMD Ryzen Threadripper PRO 9945WX (Zen 5, 12C/24T) | C (6/10) | 12 | 76 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |
| AMD Ryzen Threadripper PRO 9955WX (Zen 5, 16C/32T) | C (6/10) | 16 | 81 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |
| AMD Ryzen Threadripper PRO 9965WX (Zen 5, 24C/48T) | C (6/10) | 24 | 153 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |
| AMD Ryzen Threadripper PRO 9975WX (Zen 5, 32C/64T) | C (6/10) | 32 | 162 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |
| AMD Ryzen Threadripper PRO 9985WX (Zen 5, 64C/128T) | C (6/10) | 64 | 325 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |
| AMD Ryzen Threadripper PRO 9995WX (Zen 5, 96C/192T) | C (6/10) | 96 | 487 MB | 1191 mV | 311 A | 132 ps | 0 | 0 |

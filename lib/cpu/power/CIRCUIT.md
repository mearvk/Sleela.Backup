# IBM POWER Circuit Model

## Functional scope

This is an architectural/functional circuit model, not a transistor reconstruction.

## Blocks

1. Instruction fetch.
2. Instruction decode.
3. Branch processor.
4. General register file.
5. Fixed-point execution.
6. Floating-point execution.
7. Condition/control registers.
8. Storage/load-store interface.
9. Cache interface.
10. Exception/interrupt control.
11. Completion/control.
12. System interconnect.

## POWER2 profile

POWER2 adds execution resources represented by explicit profile components rather than being hidden in the generic POWER unit.

## Couplers

SLCoupler models:

- fetch -> branch/decode;
- decode -> register file;
- registers -> fixed-point;
- registers -> floating-point;
- branch -> control/PC;
- storage -> cache/memory;
- cache -> system interconnect;
- exception -> control state;
- DMA -> bus arbitration.

## Executable circuit model

`SLPOWERCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLPOWERCPU.circuitModel()`
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
power-on the die rail settles at 4950 mV with 14408 uV ripple after the ramp, a 556 mA peak
supply current and a 8130 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: POWER1 multi-chip. 1 core(s), 1 thread(s) per core,
superscalar in order, 4 pipeline stage(s), issue width 4. Caches: L1I 8 KB, L1D 64 KB,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 30200 ps; transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

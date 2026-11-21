# Motorola 68040 Circuit Model

## Functional scope

Architectural/functional model, not transistor reconstruction.

## Major blocks

1. Instruction fetch/decode.
2. Instruction cache.
3. Integer register file.
4. Address-generation unit.
5. Integer execution units.
6. Floating-point unit.
7. Data cache.
8. MMU/translation.
9. Branch/control.
10. Exception/interrupt control.
11. Bus/burst interface.
12. Issue/pipeline control.
13. Write-back/completion.

## Couplers

SLCoupler models:

- fetch -> I-cache/decode;
- decode -> issue;
- registers -> integer/FPU;
- address generation -> MMU;
- MMU -> I/D cache;
- D-cache -> bus;
- FPU -> completion;
- exception -> processor state;
- DMA -> bus arbitration.

## Accuracy boundary

No 68060-specific superscalar depth, branch prediction, or cache topology is projected backward onto the 68040.

## Executable circuit model

`SL68040Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SL68040CPU.circuitModel()`
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

Recalled, not checked against a source: on-chip FPU and MMU. 1 core(s), 1 thread(s) per core,
pipelined, 6 pipeline stage(s), issue width 1. Caches: L1I 4 KB, L1D 4 KB,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); named blocks explain 47.3 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

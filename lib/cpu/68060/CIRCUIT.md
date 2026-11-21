# Motorola 68060 Circuit Model

## Functional scope

Architectural/functional model, not transistor reconstruction.

## Major blocks

1. Instruction fetch.
2. Instruction buffer.
3. Decoder.
4. Branch prediction/control.
5. Dispatch/issue.
6. Data/address register files.
7. Integer execution units.
8. Address-generation units.
9. Floating-point unit.
10. Instruction cache.
11. Data cache.
12. MMU/translation.
13. Exception/interrupt control.
14. Burst bus interface.
15. Completion/control.

## Couplers

SLCoupler models:

- fetch -> instruction buffer;
- decoder -> dispatch;
- branch predictor -> fetch/dispatch;
- register files -> execution units;
- address generation -> MMU;
- MMU -> caches;
- FPU -> completion;
- D-cache -> bus;
- exception -> processor state;
- DMA -> bus arbitration.

## Accuracy boundary

No undocumented transistor-level topology or exact cycle schedule is claimed.

## Executable circuit model

`SL68060Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SL68060CPU.circuitModel()`
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
power-on the die rail settles at 3250 mV with 9295 uV ripple after the ramp, a 532 mA peak
supply current and a 8130 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: dual integer pipelines. 1 core(s), 1 thread(s) per core,
superscalar in order, 10 pipeline stage(s), issue width 2. Caches: L1I 8 KB, L1D 8 KB,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); named blocks explain 42.2 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

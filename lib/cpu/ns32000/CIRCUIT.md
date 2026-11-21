# NS32000 Circuit Model

## Functional scope

Architectural/functional model, not transistor reconstruction.

## Major blocks

1. Instruction fetch.
2. Variable-length decoder.
3. Operand/effective-address unit.
4. Register file.
5. Integer ALU.
6. Multiply/divide unit.
7. Optional floating-point interface.
8. Memory-management interface.
9. Cache interface where implemented.
10. Interrupt/exception controller.
11. Bus interface.
12. Clock/sequencing.

## Couplers

SLCoupler models:

- fetch -> decoder;
- decoder -> operand unit;
- registers -> ALU;
- address generation -> MMU/cache;
- cache -> bus;
- execution -> status;
- exceptions -> processor state;
- DMA -> bus arbitration.

## Accuracy boundary

32000 generations remain separate profiles rather than one invented universal microarchitecture.

## Executable circuit model

`SLNS32000Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLNS32000CPU.circuitModel()`
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

Recalled, not checked against a source: NS32081 FPU and NS32082 MMU external. 1 core(s), 1 thread(s) per core,
microcoded, 2 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU external, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); named blocks explain 8.6 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

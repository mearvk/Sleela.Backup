# Intel iAPX 432 Circuit Model

## Functional scope

Architectural/functional model only; this is not a transistor or microcode-ROM reconstruction.

## Major blocks

1. Instruction/control interpreter.
2. Object/descriptor unit.
3. Protection and capability checks.
4. Operand/reference unit.
5. Execution unit.
6. Memory-management interface.
7. Fault/interrupt controller.
8. System interface.
9. Clock/sequencing.

## Couplers

SLCoupler models:

- control -> object resolution;
- object resolution -> operand access;
- protection -> memory transaction;
- execution -> architectural state;
- fault -> control state;
- DMA -> system arbitration.

The model intentionally preserves the unusual 432 architecture instead of translating it into a generic x86 register machine.

## Executable circuit model

`SLiAPX432Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLiAPX432CPU.circuitModel()`
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

Recalled, not checked against a source: multi-chip set; capability-based segmented addressing. 1 core(s), 1 thread(s) per core,
microcoded, 3 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU segmented, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

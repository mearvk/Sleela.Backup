# Motorola 6809 Circuit Model

## Functional scope

Architectural functional model, not transistor-level reconstruction.

## Blocks

1. Instruction fetch and prefix handling.
2. Opcode decoder.
3. A/B accumulators and D register view.
4. X/Y index registers.
5. S/U stack pointers and PC.
6. Direct-page and effective-address logic.
7. Integer/logic ALU.
8. Condition-code and interrupt control.
9. Memory/bus interface.
10. Clock and sequencing.

## Couplers

SLCoupler models fetch-to-decode, decode-to-registers, index/DP-to-address generation, ALU-to-condition codes, stack state-to-interrupt entry, and bus-to-memory paths.

## Executable circuit model

`SL6809Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SL6809CPU.circuitModel()`
returns it built and clocked.

The class header marks every figure as recalled or derived. Recalled figures
come from general knowledge and still need checking against a primary source.
Derived figures are planning numbers, not measurements. The model is at
functional-block level, not a transistor netlist.

Circuit grade: **B** (8/10), with 0 verification violations, from
`circuitScore()` in `SLCircuitModel`. `lib/cpu/examples/circuit-grades.sleela`
grades every CPU.

## Power delivery

The circuit model carries an `SLPowerNetwork` (series R and L, decoupling C, load) on
`SLCircuitDetails`. Its values are derived planning figures named in the class. On a soft-started
power-on the die rail settles at 4980 mV with 14799 uV ripple after the ramp, a 261 mA peak
supply current and a 8209 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: single core, no caches, no FPU, no on-chip MMU. 1 core(s), 1 thread(s) per core,
sequential, 1 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); named blocks explain 14.5 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

# Z80 Circuit Model

## Scope

This is a functional circuit model, not an asserted transistor-level reconstruction.

## Major blocks

1. Register file and alternate register bank.
2. ALU and flag generation.
3. Instruction register/decode/control.
4. Address generation and index logic.
5. Internal data movement.
6. Memory/I/O bus interface.
7. Interrupt control.
8. Refresh control.
9. Clock and T/M cycle sequencing.

## Couplers

SLeeLa uses SLCoupler objects between these blocks so the architecture can expose register-to-ALU movement, address-to-bus movement, data-bus-to-register movement, interrupt acknowledge, external WAIT, and bus request/acknowledge.

The Z80's internal transistor organization is not claimed here unless supported by a validated die-level source.

## Executable circuit model

`SLZ80Circuit.sleela` carries this circuit in source: NMOS depletion-load, 5 V, single-phase clock, 4-bit ALU. It states the
logic family and voltage thresholds, the supply rail, the functional blocks with
width, depth, gate and transistor counts and derived area, the timing paths with
stage delays, the monitored nets with signal-quality checks, and the semaphores
that arbitrate the shared latch and buses. Every figure is marked published,
cited, recalled or derived in the class header. `circuit-demo.sleela` builds
the model, clocks it, verifies it and injects faults to show each check fails
when it should.

The model stays at functional-block level. It is not a transistor netlist.

Circuit grade: **B** (7/10), from `circuitScore()` in `SLCircuitModel`. `SLZ80CPU.circuitModel()` returns this circuit. Checking the recalled figures named in the class header against a primary source raises the grade.

## Power delivery

The circuit model carries an `SLPowerNetwork` (series R and L, decoupling C, load) on
`SLCircuitDetails`. Its values are derived planning figures named in the class. On a soft-started
power-on the die rail settles at 4980 mV with 14799 uV ripple after the ramp, a 261 mA peak
supply current and a 8209 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: multi-cycle machine cycles; no caches. 1 core(s), 1 thread(s) per core,
sequential, 1 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); named blocks explain 33.7 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

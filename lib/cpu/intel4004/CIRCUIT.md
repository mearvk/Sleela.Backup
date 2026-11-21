# Intel 4004 Logical Circuit Model

This SLeeLa view is a logical teaching/diagnostic model, not a transistor-level reconstruction.

Conceptual blocks:
- 12-bit program counter and return-stack logic
- instruction-byte fetch and decoder
- 4-bit accumulator and arithmetic/carry path
- sixteen 4-bit registers and pair operations
- program-store interface
- external chipset and I/O control

Shared SLeeLa gates, latches, flip-flops, adders, and bus abstractions may support visualization. Label inferred relationships as derived; do not present them as verified silicon schematics.
## Executable circuit model

`SLIntel4004Circuit.sleela` carries this circuit in source: PMOS silicon gate, 10 um, negative 15 V supply, two-phase 740 kHz. It states the
logic family and voltage thresholds, the supply rail, the functional blocks with
width, depth, gate and transistor counts and derived area, the timing paths with
stage delays, the monitored nets with signal-quality checks, and the semaphores
that arbitrate the shared latch and buses. Every figure is marked published,
cited, recalled or derived in the class header. `circuit-demo.sleela` builds
the model, clocks it, verifies it and injects faults to show each check fails
when it should.

The model stays at functional-block level. It is not a transistor netlist.

Circuit grade: **B** (7/10), from `circuitScore()` in `SLCircuitModel`. `SLIntel4004CPU.circuitModel()` returns this circuit. Checking the recalled figures named in the class header against a primary source raises the grade.

## Power delivery

The circuit model carries an `SLPowerNetwork` (series R and L, decoupling C, load) on
`SLCircuitDetails`. Its values are derived planning figures named in the class. On a soft-started
power-on the die rail settles at -14997 mV with 45165 uV ripple after the ramp, a 302 mA peak
supply current and a 8213 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: 8 clock periods per instruction cycle; no caches. 1 core(s), 1 thread(s) per core,
sequential, 1 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); named blocks explain 59.1 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

# ESA/390 Logical Circuit Abstraction

## Scope

A functional logic abstraction for simulation and teaching, not a transistor-level System/390 netlist.

## Blocks

- Instruction fetch and decode.
- General-purpose register file and execution units.
- ESA/390 PSW, condition-code, and interruption control.
- Address generation and optional translation/protection path.
- Profile-enabled floating-point and control-state units.
- Storage interface and channel-I/O request interface.

## Profile control

Instantiate only blocks justified by the selected ESA/390 model and enabled facilities. Do not infer physical gate counts, clock rate, pipeline depth, power, or undocumented cache layout.

## Executable circuit model

`SLESAS390Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLESAS390CPU.circuitModel()`
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
power-on the die rail settles at 4940 mV with 14702 uV ripple after the ramp, a 3239 mA peak
supply current and a 18357 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: ESA/390 CMOS; model not stated, so caches not modelled. 1 core(s), 1 thread(s) per core,
pipelined, 5 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

# Xtensa Functional Circuit Model

Functional block model, not a transistor-level netlist. Profile-selected blocks include fetch/decode, register file and optional window mapping, integer ALU, branch/loop unit, load/store unit, optional floating-point or DSP/SIMD units, memory protection/MMU, caches, debug/exception logic, and SoC bus interfaces. Custom instruction blocks require explicit metadata. SLCoupler models decode-to-execution, operand/writeback paths, memory traffic, exception signaling, and optional extension coupling.

## Executable circuit model

`SLXtensaCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLXtensaCPU.circuitModel()`
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
power-on the die rail settles at 3241 mV with 9416 uV ripple after the ramp, a 3125 mA peak
supply current and a 18180 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: ESP32 class dual LX6; flash cache. 2 core(s), 1 thread(s) per core,
pipelined, 5 pipeline stage(s), issue width 1. Caches: L1I 32 KB, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

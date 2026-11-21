# SuperH Circuit Model

## Functional scope

Architectural/functional model; not transistor reconstruction.

## Blocks

1. Instruction fetch.
2. Decoder.
3. Register file.
4. Status/control registers.
5. Integer ALU.
6. Multiply/accumulate unit.
7. Branch/control unit.
8. Load/store unit.
9. Optional DSP/FPU.
10. Optional MMU/TLB.
11. Optional I-cache/D-cache.
12. Exception/interrupt controller.
13. Bus interface.
14. Clock/pipeline control.

## Couplers

SLCoupler models:

- fetch -> decoder;
- decoder -> register file;
- registers -> ALU/MAC;
- branch -> PC/delay state;
- load/store -> MMU/cache;
- cache -> bus;
- FPU/DSP -> register state;
- exception -> control registers;
- DMA -> bus arbitration.

## Accuracy boundary

No SH-family profile receives undocumented cache size, pipeline depth, MMU, FPU, or DMA resources.

## Executable circuit model

`SLSuperHCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLSuperHCPU.circuitModel()`
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

Recalled, not checked against a source: SH-4. 1 core(s), 1 thread(s) per core,
superscalar in order, 5 pipeline stage(s), issue width 2. Caches: L1I 8 KB, L1D 16 KB,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 128 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

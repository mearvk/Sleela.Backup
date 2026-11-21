# DEC PDP-11 Circuit Model

## Functional scope

This is a functional architectural model, not transistor-level reconstruction.

## Major blocks

1. Instruction register/fetch.
2. Instruction decoder.
3. Register file.
4. Effective-address generator.
5. Integer/logic execution.
6. Condition-code logic.
7. Memory/I/O interface.
8. Interrupt/trap controller.
9. Optional memory-management interface.
10. System-bus interface.
11. Clock/sequencing.

## Couplers

SLCoupler represents:

- fetch -> decoder;
- decoder -> register file;
- register file -> address generation;
- address generation -> bus;
- execution -> condition codes;
- bus -> memory/I/O;
- interrupt -> processor control;
- DMA -> bus arbitration.

## Executable circuit model

`SLPDP11Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLPDP11CPU.circuitModel()`
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
power-on the die rail settles at 4950 mV with 16412 uV ripple after the ramp, a 5225 mA peak
supply current and a 185890 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: KA11, hardwired, not microcoded. 1 core(s), 1 thread(s) per core,
sequential, 1 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU none, MMU none on chip, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); transistor total unknown, so no budget check.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

## Clock correction

`SLPDP11CPU` stated 15 MHz with no source, and a 16-bit TTL add cannot fit that clock. The KA11 is
asynchronous; its nominal step is 800 ns, or 1.25 MHz (retrocomputing.stackexchange.com/questions/6960),
and the DEC PDP-11 Instruction List gives addressing-mode times of 1.4 to 3.9 us. On 2026-11-21 the CPU
class was set to 1 MHz (it stores whole MHz) and the circuit to 1250 kHz. The circuit now verifies clean.

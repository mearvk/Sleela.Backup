# Intel i860 Circuit Model

## Functional scope

Architectural/functional model, not transistor reconstruction.

## Major blocks

1. Instruction fetch.
2. Decoder and pairing logic.
3. Integer register file.
4. Integer/address execution.
5. Floating-point register file.
6. Floating-point/vector execution.
7. Load/store path.
8. MMU/TLB where implemented.
9. Instruction/data cache.
10. Branch/control logic.
11. Bus interface.
12. Clock and sequencing.

## Couplers

SLCoupler models:

- fetch -> decoder;
- decoder -> pairing;
- register files -> execution units;
- execution -> memory;
- cache/MMU -> bus;
- control transfer -> fetch;
- execution -> completion;
- DMA -> bus arbitration.

## Accuracy boundary

The i860's exposed VLIW/dual-operation behavior is modeled without inventing undocumented internal transistor topology.

## Executable circuit model

`SLI860Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLI860CPU.circuitModel()`
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

Recalled, not checked against a source: i860 XR; dual-instruction mode; 64-bit graphics unit. 1 core(s), 1 thread(s) per core,
superscalar in order, 4 pipeline stage(s), issue width 2. Caches: L1I 4 KB, L1D 8 KB,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 64 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 16250 ps; named blocks explain 93.0 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

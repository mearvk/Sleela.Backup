# Intel Itanium Circuit Model

## Scope

Functional architectural/microarchitectural model, not transistor reconstruction.

## Major blocks

1. Instruction fetch.
2. 128-bit bundle buffer.
3. Template/slot decoder.
4. General register file.
5. Floating-point register file.
6. Predicate register file.
7. Branch register file.
8. Rotating-register/RSE machinery.
9. Integer execution clusters.
10. Floating-point execution clusters.
11. Branch units.
12. Load/store units.
13. Speculation/check machinery.
14. MMU/TLB.
15. Cache hierarchy.
16. Interrupt/exception controller.
17. IA-32 compatibility subsystem where enabled.
18. System interconnect.
19. Clock/control.

## Couplers

SLCoupler models:

- bundle -> decoder;
- template -> slot classification;
- predicates -> execution units;
- registers -> execution units;
- execution -> register files;
- branch -> control state;
- RSE -> register stack;
- load/store -> MMU;
- MMU -> TLB/cache;
- speculation -> check/recovery;
- cache -> interconnect;
- DMA -> arbitration.

## Accuracy boundary

No specific Itanium generation's transistor layout, exact cluster topology, cache geometry, or pipeline depth is asserted by the generic architecture.

## Executable circuit model

`SLItaniumCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLItaniumCPU.circuitModel()`
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
power-on the die rail settles at 1751 mV with 4056 uV ripple after the ramp, a 10407 mA peak
supply current and a 25434 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: Merced; 4 MB L3 sits off die in the cartridge. 1 core(s), 1 thread(s) per core,
superscalar in order, 10 pipeline stage(s), issue width 6. Caches: L1I 16 KB, L1D 16 KB,
L1 unified 0 per core; L2 96 KB, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 64 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 578 ps; named blocks explain 31.3 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

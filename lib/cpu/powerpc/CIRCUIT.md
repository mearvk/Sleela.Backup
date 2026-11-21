# PowerPC Circuit Model

## Scope

Functional architectural circuit model; not a transistor-level reconstruction.

## Major blocks

1. 32-entry GPR file.
2. 32-entry FPR file.
3. Branch processor.
4. CR field logic.
5. LR/CTR branch state.
6. XER arithmetic status.
7. Integer/fixed-point execution unit.
8. Floating-point execution unit when implemented.
9. Load/store unit.
10. Instruction fetch/decode/dispatch.
11. Pipeline and completion state.
12. MMU/TLB when implemented.
13. Instruction/data cache interfaces when implemented.
14. Exception/interrupt control.
15. System bus interface.
16. Clock and sequencing.

## Couplers

SLCoupler models:

- GPR -> integer unit;
- FPR -> floating-point unit;
- ALU -> GPR;
- compare -> CR;
- branch unit -> PC;
- LR/CTR -> branch control;
- load/store -> cache/bus;
- MSR/SPR -> privileged control;
- exception -> vector/control state.

## Accuracy boundary

No implementation-specific cache, TLB, bus width, pipeline depth, or execution-unit count is asserted without a concrete processor source.

## Executable circuit model

`SLPowerPCCircuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLPowerPCCPU.circuitModel()`
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
power-on the die rail settles at 3540 mV with 10348 uV ripple after the ramp, a 3146 mA peak
supply current and a 18180 ns settle time, with 0 power violations.

## Design character

Recalled, not checked against a source: PowerPC 601, unified 32 KB. 1 core(s), 1 thread(s) per core,
superscalar in order, 4 pipeline stage(s), issue width 3. Caches: L1I 0, L1D 0,
L1 unified 32 KB per core; L2 0, L3 0 per chip; local store 0.
FPU on chip, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: pipeline-stage slack 9891 ps; named blocks explain 69.4 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

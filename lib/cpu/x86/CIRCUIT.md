# x86 / x86-64 Circuit Model

## Scope

Functional architectural/microarchitectural boundary model; not a transistor-level reconstruction.

## Major blocks

1. General-register file.
2. Instruction pointer.
3. Flags/condition logic.
4. Segment and descriptor state.
5. Variable-length instruction fetch/decode.
6. Prefix and opcode decoder.
7. Micro-operation/operation representation.
8. Integer execution units.
9. Address-generation units.
10. Branch/control unit.
11. Load/store unit.
12. x87/vector execution where implemented.
13. Paging/MMU.
14. TLB and paging-structure caches.
15. Cache hierarchy.
16. Control/debug/MSR state.
17. Interrupt/exception unit.
18. Retirement/completion state.
19. System interconnect.
20. Clock/control.

## Couplers

SLCoupler exposes:

- decoder -> execution operation;
- register -> ALU;
- register -> address generation;
- address -> segmentation/paging;
- paging -> TLB/cache;
- cache -> load/store;
- branch -> fetch/IP;
- flags -> conditional execution;
- control registers -> MMU;
- exception -> descriptor/interrupt state;
- retirement -> architectural register state.

## Accuracy boundary

No exact Intel or AMD internal pipeline width, cache topology, port count, transistor structure, or speculative mechanism is claimed by the generic x86 object.

## Executable circuit model

`SLX86Circuit.sleela` carries this circuit in source. It states the logic family and
voltage thresholds, the supply rail, the clock and its phases, the functional
blocks with width, depth, gate and transistor counts and derived area, two
timing paths with stage delays, four monitored nets with signal-quality
checks, and three semaphores for the shared latch and buses. `SLX86CPU.circuitModel()`
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

Recalled, not checked against a source: 80386 class. 1 core(s), 1 thread(s) per core,
microcoded, 6 pipeline stage(s), issue width 1. Caches: L1I 0, L1D 0,
L1 unified 0 per core; L2 0, L3 0 per chip; local store 0.
FPU external, MMU paged, SIMD 0 bits.
`buildDesign()` turns these into blocks with derived transistor counts. Result: no pipeline-stage check (sequential or microcoded, or feature size unknown); named blocks explain 17.5 percent of the known transistor total.

## CPU link

The circuit and its CPU class address each other: `circuitModel()` on the CPU builds this circuit and
passes the CPU name, rated clock and data width; `cpuModel()` and `linkCpu()` on the circuit build the
CPU and check the same three facts. Mismatches: 0.

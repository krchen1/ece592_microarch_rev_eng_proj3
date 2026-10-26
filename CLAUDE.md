# CLAUDE.md — ECE 492/592 Homework 3, Track B

Microarchitecture reverse engineering on two Intel machines. Source of truth: `Homework_3.pdf`. Due **Nov 3, 2026, 11:59 PM Eastern**.

## 1. Assignment summary (Track B)
- Individual work. AI is permitted but **must be disclosed**; the student must understand and be able to explain all submitted work.
- 300 points are available. Choose any combination worth **>= 100 points**. List the selected questions and their total points on the **first page** of the report.
- For each selected exercise: run on **two course machines (both Intel)** and present results **side by side**. Briefly explain the method and any differences between machines. A broad sweep, year-sorted trend analysis and a paper-style report are *not* required.
- **Enclave comparison**: run each selected experiment inside an enclave (e.g. Intel SGX) and outside it on the same enclave-capable machine; compare results and explain differences/challenges. Identify the enclave platform/config. If enclave support/access is unavailable, report that limitation (the ordinary experiments must still run on two machines).
- Identify the CPU model and microarchitecture of every tested machine. Document unsupported features and access limitations.
- Per experiment: code snippet in the report + methodology (what you test, how it works, what you measure, how results support the conclusion) + repeated measurements + plots + short explanation. **Separate measured findings from published specs.**
- **Excluded**: cache and prefetcher reverse engineering, including data-memory-dependent prefetchers (DMPs). We only *neutralize* prefetchers; we never characterize them.
- Deliverable: ZIP with PDF report, source code, raw data, plotting scripts, reproduction instructions, and an **AI-use log** (or an explicit statement that no AI was used).
- Bonus (optional, needs >= 8 machines): propose a "law" across machines. Out of scope.

## 2. Selected questions
**Selected: Q4 (30) + Q5 (35) + Q6 (35) + Q7 (15) = 115 pts.**

Why: no single question exceeds 35 pts, so at least 3 are needed. Effort is driven by distinct programs, not question count: Q4+Q5 share one ROB/PRF harness, and Q6+Q7 share one TLB pointer-chase program (Q7 is just a stride parameter). That gives **2 programs** for 115 pts. Q4+Q5+Q6 alone is exactly 100 with no margin, so Q7 is added as cheap insurance. Optional cheap add-ons reusing the TLB program: Q8 (+10), Q9 (+10).

| Q | Task | Pts | Status |
|---|------|-----|--------|
| 1 | Experiment: is the processor pipelined | 10 | not selected |
| 2 | Experiment: out-of-order execution | 10 | not selected |
| 3 | Experiment: branch prediction | 10 | not selected |
| **4** | **Reverse engineer ROB size** | **30** | **selected** |
| **5** | **Physical register file size; identify register class** | **35** | **selected** |
| **6** | **Number of TLB levels and size of each** | **35** | **selected** |
| **7** | **TLB associativity** | **15** | **selected** |
| 8 | Cost of a TLB miss | 10 | optional add-on |
| 9 | TLB behavior across supported page sizes | 10 | optional add-on |
| 10 | How many branches the BTB can store | 20 | not selected |
| 11 | BTB associativity | 20 | not selected |
| 12 | Address bits affecting BTB indexing/tagging | 20 | not selected |
| 13 | Branch predictor: neural vs counter vs hybrid | 35 | not selected |
| 14 | Instructions fetched per cycle | 10 | not selected |
| 15 | Pipeline depth | 10 | not selected |
| 16 | ROB and BTB shared vs partitioned under SMT | 20 | not selected |

## 3. Machines (user fills in; never guess)
| | Machine A: artemisia | Machine B: crux |
|---|---|---|
| CPU model | Intel Xeon Gold 5420+ | Intel Core i7-9700 |
| Microarchitecture | Sapphire Rapids (Golden Cove) | Coffee Lake (Skylake-family) |
| Cores / SMT | | |
| OS / kernel | | |
| Microcode | | |
| SGX available? | | |
| Root access? | | |
| MSR access (`msr` module)? | | |
| THP state | | |
| Spectre mitigations | | |

Record `lscpu`, `cpuid`, `/sys/devices/system/cpu/vulnerabilities/*` and `cat /proc/cmdline` into `env.txt` inside every run directory (done automatically by `scripts/capture_env.sh`). Keep the same table in `docs/machines.md`.

## 4. Repo layout (create as needed)
```
src/        common.h (timing, pinning, stats), q4_q5_rob_prf.c, q6_q7_tlb.c
scripts/    run_all.sh <machine>, plot.py, env capture
data/<machine>/<YYYYmmdd-HHMMSS>_<label>/   one directory per run: raw CSVs, logs, env.txt, cmd.txt
data/<machine>/latest -> newest run_all.sh run
plots/      generated figures
report/     report source and final PDF
docs/machines.md
AI_LOG.md   append an entry for each substantive AI contribution
REPRODUCE.md
Makefile    targets: all, run, plot, zip
```

## 5. Measurement harness rules
- Pin to one physical core (`sched_setaffinity` / `taskset -c N`); fix frequency (performance governor, disable turbo if possible); keep the SMT sibling idle.
- Time with `lfence; rdtscp; lfence` (or `rdpmc` core cycles). TSC != core clock; prefer core-cycle counters or calibrate. Warm up, then >= 30 trials; report **median plus min/IQR**; plot error bars.
- Compile `-O2`, but keep timed regions in inline asm / generated code so the compiler cannot reorder, fold or remove them. Generate straight-line code (script emitting asm, or an `mmap`'d executable buffer) rather than loops when a loop branch would pollute the test.
- Never use `xor r,r` / `mov r,0` zeroing idioms or `mov r,r` in register-pressure tests: the renamer eliminates them and they consume no physical register.
- Save raw data to CSV before plotting; plots come from scripts, never hand-edited.

## 5b. Raw data retention (every run is saved)
- **Every run of an experiment program saves its raw data**, including test, zoom, failed and discarded runs. Nothing is run "just to look" without saving.
- Each run gets a **new** directory `data/<machine>/<YYYYmmdd-HHMMSS>_<label>/` containing the per-trial CSV(s), the stderr log(s), `env.txt` (machine state) and `cmd.txt` (exact command lines).
- Use `scripts/run_all.sh <machine>` for the full set (label `all`; updates the `data/<machine>/latest` symlink) and `scripts/run_exp.sh <machine> <label> <program> [args...]` for any single/ad-hoc run. Do not call the binaries with `-o` into `data/` by hand, and never write to stdout only.
- **Never overwrite, edit or delete** an existing run directory. A bad run stays; explain in the report why it was excluded.
- `scripts/plot.py` reads `latest` by default (or `machine:run_dir`); the report must name the run directory each figure came from.
- Development-host test runs (not artemisia/crux) go to the scratchpad, never into `data/`.

## 5a. Performance counter policy
- **Reverse-engineering phase: NO performance counters.** Do not use `perf`, `rdpmc`, PMU events, or any hardware counter to *derive* ROB/PRF/TLB sizes, associativity, or any other result. Conclusions must come from timing alone (`lfence; rdtscp; lfence` etc.).
- **Confirmation phase only:** counters (`perf stat`, `rdpmc`, `l2_rqsts.*`, `dtlb_load_misses.*`, etc.) may be used afterwards solely to *confirm* a result already obtained from timing (e.g. verify a latency step is a TLB miss, verify prefetchers are neutralized). Label such data "confirmation" in the report and keep it in separate files/columns from the timing-derived results.
- Never tune experiment parameters using counter feedback; counters validate, they do not discover.
- This overrides any suggestion elsewhere in this file to use counters (e.g. the `perf stat` sanity checks in section 7 and `rdpmc` timing in section 5): those apply to the confirmation phase only.

## 6. Method notes
### Selected
- **Q4 ROB** (`q4_q5_rob_prf.c`): cache-missing load (flushed with `clflush`, pointer-chased) → N independent 1-uop fillers (`nop`, unrolled straight-line) → second cache-missing load. Plot time vs N; the knee where the second miss can no longer overlap is the ROB size. Discuss other limits (scheduler, PRF, load buffer) that could cap the knee earlier; NOP fillers avoid PRF/scheduler pressure.
- **Q5 PRF** (same program): same structure, but fillers are register-writing instructions of one class: GPR (`add` to distinct regs), SIMD (`vpaddd`/`vpxor`), mask `k` regs, flags. A knee below the ROB size identifies that class's physical register file (PRF size ≈ knee + architectural registers). Compare classes to find which limits and report the identified class. Each filler must write a register but not be eliminated by renaming.
- **Q6 TLB levels/sizes** (`q6_q7_tlb.c`): randomly permuted pointer chase touching **one cache line per page** across P pages; vary the in-page offset per page so lines don't alias cache sets. Latency steps vs P give L1 dTLB and STLB reach (entries = P at each step). Footprint of lines stays small enough to be cache-resident, so steps are TLB-only.
- **Q7 TLB associativity** (same program): stride of 2^k pages over N pages. When conflict misses appear before capacity, ways = entries / sets; use the Q6 sizes to derive sets and ways. Report uncertainty (index hashing on some Intel STLBs can blur the result).

### Not selected (reference only)
- Q8: difference between latency plateaus (L1 miss/STLB hit vs page walk) from the Q6 data. Q9: rerun Q6/Q7 with 2 MB (`MAP_HUGETLB`/`MADV_HUGEPAGE`) and 1 GB pages.
- Q1: dependent vs independent chains. Q2: independent work behind a long-latency op. Q3: predictable vs pre-generated random branch.
- Q10–12: generated-code taken-branch chains with fixed/power-of-two strides at controlled virtual addresses. Q13: long-period / XOR-parity history patterns. Q14: unrolled independent NOPs IPC plateau. Q15: mispredict penalty. Q16: Q4/Q10 with sibling thread busy.

## 7. Avoiding speculation, prefetch and TLB confounders
Apply per experiment. *Verify every item on the actual target CPU; mark unverified claims as such in the report.*

**Hardware data prefetchers** (L2 streamer, L2 adjacent-line, DCU next-line, DCU IP-stride):
- Preferred (needs root + `modprobe msr`): disable via `MSR 0x1A4` bits 0–3 (`wrmsr -a 0x1a4 0xf`; `rdmsr 0x1a4` first, save the value, **restore afterwards**). Bit meanings are model-specific; check Intel docs for the target microarchitecture. If MSR is locked, try BIOS settings.
- Without root (likely on shared course machines), defeat in software:
  - **Randomly permuted pointer chase**: next address depends on loaded data, so there is no stream or constant stride to learn.
  - One access per page; never touch both lines of a 128 B aligned pair (adjacent-line prefetcher) or neighboring lines/pages in sequence.
  - Randomize the in-page offset per page; never visit pages in ascending/descending order.
- Sanity check: sequential vs random chase over the same footprint should give the same latency when prefetchers are neutralized. Confirm with `perf stat` (`l2_rqsts.*`, `dtlb_load_misses.*`) where available.
- In Q4/Q5 the two miss loads must be random addresses far apart so no prefetcher turns the second miss into a hit (that would fake a larger ROB).

**TLB prefetch / page-crossing prefetch**: random page order, never adjacent pages in sequence.

**TLB confounders**: force 4 KB pages: `madvise(MADV_NOHUGEPAGE)` and, if root, `echo never > /sys/kernel/mm/transparent_hugepage/enabled`. Pre-touch (`MAP_POPULATE` or a write pass) to remove page-fault cost. Avoid syscalls inside the timed region (KPTI/PCID flushes). Pin to prevent migrations. Page-walk caches (PDE etc.) affect walk cost; mention their effect when interpreting.

**Branch speculation**: Q4–Q7 need no branch tests, so keep the timed region branch-free (unrolled) or with a perfectly predictable loop branch. Use direct jumps only. Record mitigation state (IBRS/STIBP/retpoline); it can change results.

**Memory speculation**: avoid store→load aliasing in timed regions (disambiguation / forwarding noise).

**Front-end**: `.align 64` generated blocks; large unrolled filler may exceed the uop cache, so note whether fetch/decode (not ROB) limits the filler rate.

**Value/pointer prediction (DMP-style)**: not studied, but neutralize: store *indices* or XOR-masked pointers rather than raw pointer-looking values in the chase array where feasible.

**Noise**: pin IRQs, `isolcpus`/`nohz_full` if available, SMT sibling idle, >= 30 trials, use median.

## 8. SGX / enclave notes
- Both programs run in an enclave (Intel SGX SDK; ECALL wrapper around the same kernel) on the SGX machine, and outside on the same machine. Identify platform (CPU, SGX1/SGX2, SDK version, driver, hardware vs simulation mode).
- `rdtsc`/`rdtscp` may be illegal inside SGX1 enclaves; use a counter thread outside the enclave or OCALL timestamps, and document the resolution loss.
- Expect differences: `clflush` limits, no huge-page control, EPC paging, enclave transitions (AEX) adding noise. Explain honestly.
- If SGX is unavailable on both machines, state the limitation with evidence (`cpuid` SGX bit, BIOS setting, driver absence).

## 9. Workflow
1. Write `src/q4_q5_rob_prf.c` and `src/q6_q7_tlb.c` using `common.h`. 2. Run on machine A and B (user runs on real hardware; Claude never invents numbers). 3. Raw CSVs land in a new `data/<machine>/<timestamp>_<label>/` per run (§5b). 4. Plot A and B side by side. 5. Write short methodology + comparison + uncertainty. 6. Repeat inside the enclave on the SGX machine. 7. Append to `AI_LOG.md`.

## 10. Rules for Claude
- Never fabricate measurements; published ROB/PRF/TLB sizes go in a separate "published" column and are labeled as such.
- Ask the user for machine details before writing experiments.
- Keep code simple enough for the student to explain line by line; comment the *why* of each timed region.
- **AI log (mandatory):** before ending any session that created or changed files, generated code, analysis or report text, append an entry to `AI_LOG.md` using the entry format at the top of that file (date, model, request, contribution with file list; leave "Student review/changes" for the student to fill in). Append only; never edit or delete earlier entries. Q&A-only sessions with no artifacts need no entry. The final ZIP must contain `AI_LOG.md`.
- Do not commit unless asked. Commit messages end with the attribution line from the session context.

## 11. Commands (to be created)
- Build: `make`
- Run: `CPU=N ./scripts/run_all.sh <machine>` (full set) or `CPU=N ./scripts/run_exp.sh <machine> <label> <program> [args...]` (single run)
- Plot: `python3 scripts/plot.py [machine[:run_dir] ...]`
- Package: `make zip`

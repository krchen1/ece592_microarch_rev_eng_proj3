# AI-Use Log — ECE 492/592 Homework 3, Track B

AI assistance was used in this project, as permitted by the assignment, and is disclosed here.
The student reviewed all AI-generated material and can explain all submitted work.

**Tool:** Claude Code (Anthropic), model noted per entry.

**Policy:**
- AI never produces measurement data. Every number in `data/` comes from runs on real hardware performed by the student.
- Published specifications (ROB/PRF/TLB sizes) suggested by AI are kept separate from measured results and labeled "published".
- Performance counters are used only to confirm timing-derived results, never to derive them (see `CLAUDE.md` §5a).

## Entry format
Append new entries at the bottom, newest last. One entry per session or substantive contribution:

```
### YYYY-MM-DD — <short title>
- **Model:** <model name/ID>
- **Prompt/request:** <what the student asked for, paraphrased>
- **AI contribution:** <what the AI produced or changed; list files>
- **Student review/changes:** <what the student verified, modified or rejected>
```

## Entries

### 2026-10-26 — Project setup (CLAUDE.md)
- **Model:** _(student to fill in: whether AI drafted `CLAUDE.md`, and which tool)_
- **Prompt/request:** Project plan and assistant instructions for Track B (Q4, Q5, Q6, Q7).
- **AI contribution:** _(student to fill in)_
- **Student review/changes:** _(student to fill in)_

### 2026-10-26 — Create AI-use log
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`) via Claude Code
- **Prompt/request:** "create ai use log. make sure future claude sessions adds to this log"
- **AI contribution:** Created `AI_LOG.md` (this file, with entry format). Added a mandatory logging rule to `CLAUDE.md` §10 so future Claude Code sessions append entries.
- **Student review/changes:** _(student to fill in)_

### 2026-10-26 — Q4/Q5 and Q6/Q7 experiment programs
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`) via Claude Code
- **Prompt/request:** "build the 2 program code for Q4, Q5, Q6, Q7; follow the method notes to mitigate noise and effects of other hardware components." Machines given by student: artemisia (Xeon Gold 5420+, Sapphire Rapids) and crux (Core i7-9700, Coffee Lake).
- **AI contribution:** Wrote `src/common.h` (serialized TSC timing, pinning, RNG, permutations, TSC-to-core-cycle calibration via a dependent `imul` chain, THP check), `src/q4_q5_rob_prf.c` (run-time generated straight-line code: two independent DRAM-missing pointer chases separated by N fillers; filler types nop/gpr/flags/vec/mask/mix), `src/q6_q7_tlb.c` (one-line-per-page random pointer chase; `levels` mode with a cache-only control layout, `assoc` mode with power-of-two page strides), `scripts/run_all.sh` (env capture + all runs, optional opt-in MSR 0x1A4 prefetcher disable), `scripts/plot.py` (side-by-side plots + per-point median/min/IQR tables), `Makefile`, `REPRODUCE.md`, `docs/machines.md`, `.gitignore`; filled CPU model/microarchitecture rows in `CLAUDE.md` §3. Verified generated machine code by disassembly and ran short smoke tests on the development host (Xeon E5-2680 v3, not a course target machine) only to check the programs work; no data from that host is included in `data/`.
- **Student review/changes:** _(student to fill in)_

### 2026-10-26 — Per-run raw data directories
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`) via Claude Code
- **Prompt/request:** "create directory to output raw data. update claude md to save data for every run"
- **AI contribution:** Created `data/artemisia/` and `data/crux/`. Changed `scripts/run_all.sh` to write each run to a new `data/<machine>/<timestamp>_all/` directory (with `cmd.txt`, `latest` symlink); moved environment capture to new `scripts/capture_env.sh`; added `scripts/run_exp.sh` for single/ad-hoc runs saved the same way; `scripts/plot.py` now reads `latest` or a pinned run (`machine:run_dir`). Added `CLAUDE.md` §5b (raw-data retention rule) and updated §3, §4, §9, §11; updated `REPRODUCE.md`.
- **Student review/changes:** _(student to fill in)_

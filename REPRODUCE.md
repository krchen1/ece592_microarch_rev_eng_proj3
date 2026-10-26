# Reproducing the results

Requirements: Linux x86-64, gcc, make, python3 with numpy + matplotlib.
Optional: `cpuid` (recorded in env.txt), msr-tools + root (prefetcher MSR).

```sh
make                                   # builds bin/q4_q5_rob_prf, bin/q6_q7_tlb
CPU=2 ./scripts/run_all.sh artemisia   # on artemisia (Xeon Gold 5420+)
CPU=2 ./scripts/run_all.sh crux        # on crux (Core i7-9700)
python3 scripts/plot.py artemisia crux # figures + summary tables in plots/
make zip                               # submission archive
```

Choose a `CPU` whose SMT sibling is idle (`run_all.sh` records the sibling
list in `data/<machine>/env.txt`). Avoid running while the machine is busy;
`env.txt` records `uptime`.

## Where raw data goes
Every run writes a new directory and never overwrites an old one:

```
data/<machine>/<YYYYmmdd-HHMMSS>_all/     from run_all.sh
data/<machine>/<YYYYmmdd-HHMMSS>_<label>/ from run_exp.sh
data/<machine>/latest -> newest run_all.sh directory
```

Each run directory also gets `plots/` (PNGs and summary tables of that run
alone, made by `plot.py --file`) and `plot.log`.

The side-by-side figures in `plots/` come from `plot.py`, which uses `latest`
by default; pin another run with
`python3 scripts/plot.py artemisia:20261027-101500_all crux`.

## Files in a run directory
| file | content |
|---|---|
| `env.txt` | lscpu, kernel cmdline, microcode, mitigations, THP, governor, SGX/MSR access, git commit |
| `cmd.txt` | exact command line(s) that produced the CSVs |
| `q4_q5_rob_prf.csv` | one row per trial: filler, N, TSC ticks, core cycles per block |
| `q6_levels.csv` | one row per trial: layout (tlb/control), pages, cycles per load |
| `q6_levels_seq.csv` | sanity check: sequential page order |
| `q7_assoc.csv` | one row per trial: stride (pages), pages, cycles per load |
| `*.log` | the programs' stderr (per-point medians, THP check) |

## Zooming in on a step
After the default sweep shows where a step is, re-run a finer linear sweep
through `run_exp.sh` so it is saved like every other run (`-o` is added for you):

```sh
CPU=2 scripts/run_exp.sh <m> q4_zoom_nop bin/q4_q5_rob_prf -f nop -n A:B:1 -c 2
CPU=2 scripts/run_exp.sh <m> q6_zoom     bin/q6_q7_tlb levels -r A:B:1 -c 2
```

## Checking the generated Q4/Q5 code
```sh
bin/q4_q5_rob_prf -f vec -n 8:8:1 -d /tmp/k.bin
objdump -D -b binary -mi386:x86-64 /tmp/k.bin
```

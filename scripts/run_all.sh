#!/usr/bin/env bash
# run_all.sh <machine> -- capture the environment and run Q4/Q5 and Q6/Q7.
#
# Every invocation writes to a NEW directory data/<machine>/<YYYYmmdd-HHMMSS>_all/
# so no earlier run is ever overwritten. data/<machine>/latest points to it.
#
# Environment variables:
#   CPU=N          logical CPU to pin to (default 2). Keep its SMT sibling idle.
#   TRIALS=N       trials per point (default 31)
#   PREFETCH_OFF=1 (root only) disable the four L2/DCU hardware prefetchers on
#                  CPU through MSR 0x1A4 for the run, restoring the old value
#                  afterwards. Bit layout is model-specific: verify for the
#                  target CPU before using. Off by default; the experiments
#                  already defeat prefetchers in software (random chases).
set -euo pipefail

MACHINE=${1:?usage: run_all.sh <machine>}
CPU=${CPU:-2}
TRIALS=${TRIALS:-31}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="$ROOT/bin"
RUN="$(date +%Y%m%d-%H%M%S)_all"
OUT="$ROOT/data/$MACHINE/$RUN"
mkdir -p "$OUT"
ln -sfn "$RUN" "$ROOT/data/$MACHINE/latest"

make -C "$ROOT" all >/dev/null

"$ROOT/scripts/capture_env.sh" "$OUT" "$CPU"
ENV="$OUT/env.txt"
{
    echo "### run_all settings"
    echo "CPU=$CPU TRIALS=$TRIALS PREFETCH_OFF=${PREFETCH_OFF:-0}"
} >> "$ENV"
echo "run directory -> $OUT"

# ------------------------------------------------- optional prefetch disable
if [ "${PREFETCH_OFF:-0}" = 1 ]; then
    command -v rdmsr >/dev/null || { echo "PREFETCH_OFF needs msr-tools"; exit 1; }
    OLD=$(rdmsr -p "$CPU" 0x1a4)
    echo "MSR 0x1a4 on cpu $CPU was 0x$OLD; writing 0xf" | tee -a "$ENV"
    trap 'wrmsr -p "$CPU" 0x1a4 0x$OLD; echo "MSR 0x1a4 restored to 0x$OLD"' EXIT
    wrmsr -p "$CPU" 0x1a4 0xf
fi

# ------------------------------------------------------------ experiments
# Each command line is also appended to cmd.txt for reproduction.
run() {
    local name=$1; shift
    echo "$*" >> "$OUT/cmd.txt"
    "$@" -o "$OUT/$name.csv" 2> "$OUT/$name.log"
}

# Q4 + Q5: every filler type (mask is skipped automatically without AVX-512)
run q4_q5_rob_prf "$BIN/q4_q5_rob_prf" -f all -n 0:800:4 -t "$TRIALS" -c "$CPU"

# Q6: TLB levels/sizes (tlb + control layouts)
run q6_levels "$BIN/q6_q7_tlb" levels -t "$TRIALS" -c "$CPU"

# Sanity check: sequential page order over the same footprint. If it is much
# faster than the random order, prefetchers would help a predictable order;
# the random-order results are the ones used for conclusions.
run q6_levels_seq "$BIN/q6_q7_tlb" levels --seq --no-control -q 4 \
    -t "$TRIALS" -c "$CPU"

# Q7: associativity (stride 2^0 .. 2^14 pages, 1..80 pages)
run q7_assoc "$BIN/q6_q7_tlb" assoc -t "$TRIALS" -c "$CPU" -l 32768

echo "done: results in $OUT"

#!/usr/bin/env bash
# run_exp.sh <machine> <label> <program> [args...] -- one ad-hoc run (e.g. a
# zoomed sweep) saved like run_all.sh: a NEW directory
# data/<machine>/<YYYYmmdd-HHMMSS>_<label>/ holding env.txt, cmd.txt,
# <label>.csv and <label>.log. Nothing earlier is overwritten.
#
# example:
#   CPU=2 scripts/run_exp.sh crux q4_zoom_nop bin/q4_q5_rob_prf -f nop -n 200:240:1 -c 2
set -euo pipefail

MACHINE=${1:?usage: run_exp.sh <machine> <label> <program> [args...]}
LABEL=${2:?usage: run_exp.sh <machine> <label> <program> [args...]}
shift 2
[ $# -ge 1 ] || { echo "usage: run_exp.sh <machine> <label> <program> [args...]"; exit 1; }
CPU=${CPU:-2}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/data/$MACHINE/$(date +%Y%m%d-%H%M%S)_$LABEL"
mkdir -p "$OUT"

make -C "$ROOT" all >/dev/null
"$ROOT/scripts/capture_env.sh" "$OUT" "$CPU"
echo "$*" > "$OUT/cmd.txt"
"$@" -o "$OUT/$LABEL.csv" 2> "$OUT/$LABEL.log"
echo "done: results in $OUT"

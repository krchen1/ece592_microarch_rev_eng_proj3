#!/usr/bin/env bash
# capture_env.sh <out_dir> <cpu> -- record the machine state for one run.
# Writes <out_dir>/env.txt. Called by run_all.sh and run_exp.sh.
set -uo pipefail

OUT=${1:?usage: capture_env.sh <out_dir> <cpu>}
CPU=${2:?usage: capture_env.sh <out_dir> <cpu>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)

{
    echo "### date";        date -Is
    echo "### hostname";    hostname
    echo "### git";         git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo n/a
    git -C "$ROOT" status --short src scripts 2>/dev/null
    echo "### uname";       uname -a
    echo "### cmdline";     cat /proc/cmdline
    echo "### pinned cpu";  echo "$CPU"
    echo "### smt siblings of cpu $CPU"
    cat "/sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list"
    echo "### governor cpu $CPU"
    cat "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_governor" 2>/dev/null || echo n/a
    echo "### intel_pstate no_turbo"
    cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo n/a
    echo "### THP enabled / defrag"
    cat /sys/kernel/mm/transparent_hugepage/enabled
    cat /sys/kernel/mm/transparent_hugepage/defrag
    echo "### microcode";   grep -m1 microcode /proc/cpuinfo || echo n/a
    echo "### lscpu";       lscpu
    echo "### vulnerabilities"
    grep -H . /sys/devices/system/cpu/vulnerabilities/* || true
    echo "### sgx"
    grep -o -w -m1 'sgx\|sgx_lc' /proc/cpuinfo | sort -u || echo "no sgx flag in /proc/cpuinfo"
    ls -l /dev/sgx_enclave /dev/sgx_provision /dev/isgx 2>&1 || true
    echo "### msr access"
    if [ -r /dev/cpu/$CPU/msr ]; then echo "/dev/cpu/$CPU/msr readable"; else echo "/dev/cpu/$CPU/msr not readable (no root or msr module)"; fi
    echo "### cpuid"
    if command -v cpuid >/dev/null; then cpuid -1; else echo "cpuid tool not installed"; fi
    echo "### load"
    uptime
} > "$OUT/env.txt" 2>&1

#!/usr/bin/env python3
"""plot.py -- side-by-side plots for Q4/Q5 and Q6/Q7 from raw CSVs.

Each run lives in its own directory data/<machine>/<timestamp>_<label>/.
For each experiment file (q4_q5_rob_prf, q6_levels, q6_levels_seq, q7_assoc)
this script uses data/<machine>/latest/ if the file is there, otherwise the
newest run directory that contains it. Pin a run with machine:run_dir.
Writes figures to plots/ plus per-point statistics (median, min, q1, q3 over
trials) to plots/summary_<machine>_<exp>.csv.

--file plots only the given CSVs (one run of one machine). The experiment
type is detected from the columns. run_all.sh and run_exp.sh use it with
HW3_PLOTS=<run_dir>/plots so every run gets its own PNGs.

usage: python3 scripts/plot.py [machine[:run_dir] ...]  (default: artemisia crux)
       python3 scripts/plot.py --file data/<machine>/<run>/<x>.csv [...]
"""
import csv
import os
import sys
import textwrap
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.environ.get("HW3_DATA", os.path.join(ROOT, "data"))
PLOTS = os.environ.get("HW3_PLOTS", os.path.join(ROOT, "plots"))

# Fixed categorical colours: each series keeps its colour on every plot.
SERIES_COLOR = {
    "nop": "#2a78d6", "gpr": "#eb6834", "flags": "#1baf7a",
    "vec": "#eda100", "mask": "#e87ba4", "mix": "#008300",
    "tlb": "#2a78d6", "control": "#eb6834", "tlb_seq": "#1baf7a",
}
# Ordinal blue ramp (light -> dark) for the Q7 stride lines.
BLUE_RAMP = ["#9ec5f4", "#6da7ec", "#3987e5", "#2a78d6", "#256abf",
             "#1c5cab", "#184f95", "#104281", "#0d366b"]
INK, MUTED, GRID = "#222222", "#666666", "#e5e5e5"

plt.rcParams.update({
    "axes.edgecolor": MUTED, "axes.labelcolor": INK, "xtick.color": MUTED,
    "ytick.color": MUTED, "axes.grid": True, "grid.color": GRID,
    "grid.linewidth": 0.8, "axes.spines.top": False,
    "axes.spines.right": False, "font.size": 10, "lines.linewidth": 2,
})


PINNED_RUN = {}   # machine -> run directory name given as machine:run_dir
FILE_OVERRIDE = {}  # experiment name -> csv path, set by --file


def find_csv(machine, name):
    """Path of <name>.csv for this machine, following the rules above."""
    if FILE_OVERRIDE:
        return FILE_OVERRIDE.get(name)
    mdir = os.path.join(DATA, machine)
    if machine in PINNED_RUN:
        candidates = [PINNED_RUN[machine]]
    else:
        runs = sorted((d for d in os.listdir(mdir) if d != "latest"
                       and os.path.isdir(os.path.join(mdir, d))),
                      reverse=True) if os.path.isdir(mdir) else []
        candidates = ["latest"] + runs
    for run in candidates:
        path = os.path.join(mdir, run, name + ".csv")
        if os.path.exists(path):
            return path
    return None


def panel_title(machine, *names):
    """Machine name plus the run directory the panel's data came from."""
    for name in names:
        path = find_csv(machine, name)
        if path:
            run = os.path.dirname(os.path.realpath(path))
            return "%s\n%s" % (machine, os.path.basename(run))
    return machine


def csv_kind(path):
    """Experiment name of a CSV, detected from its columns and values."""
    with open(path) as f:
        rows = list(csv.DictReader(f))
    if not rows:
        sys.exit("empty csv: " + path)
    if "filler" in rows[0]:
        return "q4_q5_rob_prf"
    if {r["layout"] for r in rows} == {"tlb_seq"}:
        return "q6_levels_seq"
    if len({r["stride_pages"] for r in rows}) > 1:
        return "q7_assoc"
    return "q6_levels"


def load(machine, name):
    path = find_csv(machine, name)
    if path is None:
        return None
    print("using", os.path.relpath(path, ROOT))
    with open(path) as f:
        return list(csv.DictReader(f))


def summarize(rows, keys, value, machine, exp):
    """Group trials by keys -> {key tuple: (median, min, q1, q3, n)};
    also save the table so the plotted numbers can be checked."""
    groups = defaultdict(list)
    for r in rows:
        groups[tuple(r[k] for k in keys)].append(float(r[value]))
    stats = {}
    for k, v in groups.items():
        a = np.array(v)
        stats[k] = (np.median(a), a.min(), np.percentile(a, 25),
                    np.percentile(a, 75), len(a))
    os.makedirs(PLOTS, exist_ok=True)
    with open(os.path.join(PLOTS, f"summary_{machine}_{exp}.csv"), "w",
              newline="") as f:
        w = csv.writer(f)
        w.writerow(list(keys) + ["median", "min", "q1", "q3", "trials"])
        for k in sorted(stats, key=lambda t: tuple(_num(x) for x in t)):
            w.writerow(list(k) + ["%.4f" % x for x in stats[k][:4]]
                       + [stats[k][4]])
    return stats


def _num(x):
    try:
        return (0, float(x))
    except ValueError:
        return (1, x)


def series(stats, match, xkey_index):
    """Pick points whose key matches `match` (dict index->value), sorted by x.
    Returns x, median, lower error (median-q1), upper error (q3-median), min."""
    pts = sorted((float(k[xkey_index]), s) for k, s in stats.items()
                 if all(k[i] == v for i, v in match.items()))
    if not pts:
        return None
    x = np.array([p[0] for p in pts])
    med = np.array([p[1][0] for p in pts])
    lo = med - np.array([p[1][2] for p in pts])
    hi = np.array([p[1][3] for p in pts]) - med
    mn = np.array([p[1][1] for p in pts])
    return x, med, lo, hi, mn


def plot_line(ax, s, label, color):
    x, med, lo, hi, mn = s
    ax.errorbar(x, med, yerr=[lo, hi], color=color, label=label,
                elinewidth=1, capsize=0)
    ax.plot(x, mn, linestyle="none", marker=".", markersize=3, color=color,
            alpha=0.5)


def side_by_side(machines, title):
    fig, axes = plt.subplots(1, len(machines), figsize=(6 * len(machines), 4.2),
                             squeeze=False)
    # Wrap so the title fits a single-panel (--file) figure too.
    fig.suptitle(textwrap.fill(title, 55 * len(machines)), color=INK)
    return fig, axes[0]


def save(fig, name):
    os.makedirs(PLOTS, exist_ok=True)
    fig.tight_layout()
    path = os.path.join(PLOTS, name)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print("wrote", path)


# ---------------------------------------------------------------- Q4 / Q5
def plot_rob_prf(machines):
    fig, axes = side_by_side(
        machines, "Q4/Q5: time per block vs fillers between two cache misses "
                  "(median, IQR bars, dots = min)")
    for ax, m in zip(axes, machines):
        rows = load(m, "q4_q5_rob_prf")
        ax.set_title(panel_title(m, "q4_q5_rob_prf"), color=INK)
        ax.set_xlabel("fillers N between the two misses")
        ax.set_ylabel("core cycles per block")
        if not rows:
            ax.text(0.5, 0.5, "no data", ha="center", transform=ax.transAxes)
            continue
        st = summarize(rows, ("filler", "n"), "cycles_per_block", m, "q4_q5")
        for f in ("nop", "gpr", "flags", "vec", "mask", "mix"):
            s = series(st, {0: f}, 1)
            if s:
                plot_line(ax, s, f, SERIES_COLOR[f])
        ax.legend(frameon=False)
    save(fig, "q4_q5_rob_prf.png")


# ---------------------------------------------------------------- Q6
def plot_levels(machines):
    fig, axes = side_by_side(
        machines, "Q6: pointer-chase latency vs pages touched (one line per "
                  "page; control = same lines packed 64/page)")
    for ax, m in zip(axes, machines):
        ax.set_title(panel_title(m, "q6_levels", "q6_levels_seq"), color=INK)
        ax.set_xscale("log", base=2)
        ax.set_xlabel("pages (tlb) / lines (control)")
        ax.set_ylabel("core cycles per load")
        rows = (load(m, "q6_levels") or []) + (load(m, "q6_levels_seq") or [])
        if not rows:
            ax.text(0.5, 0.5, "no data", ha="center", transform=ax.transAxes)
            continue
        st = summarize(rows, ("layout", "pages"), "cycles_per_load", m, "q6")
        for lay, label in (("tlb", "tlb (random order)"),
                           ("control", "control (cache only)"),
                           ("tlb_seq", "tlb, sequential order (sanity)")):
            s = series(st, {0: lay}, 1)
            if s:
                plot_line(ax, s, label, SERIES_COLOR[lay])
        ax.legend(frameon=False)
    save(fig, "q6_levels.png")


# ---------------------------------------------------------------- Q7
def plot_assoc(machines):
    loaded = {m: load(m, "q7_assoc") for m in machines}
    stats = {m: summarize(r, ("stride_pages", "pages"), "cycles_per_load", m,
                          "q7") for m, r in loaded.items() if r}

    # Heat map: stride (rows) x pages (columns), colour = median latency.
    fig, axes = side_by_side(
        machines, "Q7: median cycles per load, stride 2^k pages x N pages")
    for ax, m in zip(axes, machines):
        ax.set_title(panel_title(m, "q7_assoc"), color=INK)
        ax.grid(False)
        if m not in stats:
            ax.text(0.5, 0.5, "no data", ha="center", transform=ax.transAxes)
            continue
        st = stats[m]
        strides = sorted({int(k[0]) for k in st})
        pages = sorted({int(k[1]) for k in st})
        grid = np.full((len(strides), len(pages)), np.nan)
        for (s, p), v in st.items():
            grid[strides.index(int(s)), pages.index(int(p))] = v[0]
        im = ax.imshow(grid, aspect="auto", origin="lower", cmap="Blues",
                       extent=(pages[0] - 0.5, pages[-1] + 0.5, -0.5,
                               len(strides) - 0.5))
        ax.set_yticks(range(len(strides)))
        ax.set_yticklabels(["2^%d" % int(np.log2(s)) for s in strides])
        ax.set_xlabel("pages N")
        ax.set_ylabel("stride (pages)")
        fig.colorbar(im, ax=ax, label="core cycles per load")
    save(fig, "q7_assoc_heatmap.png")

    # Line view of every other power-of-two stride (ordinal blue ramp).
    fig, axes = side_by_side(
        machines, "Q7: latency vs pages for selected strides (median, IQR)")
    for ax, m in zip(axes, machines):
        ax.set_title(panel_title(m, "q7_assoc"), color=INK)
        ax.set_xlabel("pages N")
        ax.set_ylabel("core cycles per load")
        if m not in stats:
            ax.text(0.5, 0.5, "no data", ha="center", transform=ax.transAxes)
            continue
        st = stats[m]
        strides = sorted({int(k[0]) for k in st})[::2][:len(BLUE_RAMP)]
        for i, s in enumerate(strides):
            ser = series(st, {0: str(s)}, 1)
            if ser:
                plot_line(ax, ser, "stride %d" % s, BLUE_RAMP[i])
        ax.legend(frameon=False, fontsize=8)
    save(fig, "q7_assoc_lines.png")


def plot_files(paths):
    """--file mode: one panel per figure, only for the experiments given.
    The machine name is taken from data/<machine>/<run>/<file>.csv."""
    for p in paths:
        kind = csv_kind(p)
        if kind in FILE_OVERRIDE:
            sys.exit("two %s files given: %s, %s" % (kind, FILE_OVERRIDE[kind], p))
        FILE_OVERRIDE[kind] = p
    machine = os.path.basename(os.path.dirname(os.path.dirname(
        os.path.realpath(paths[0]))))
    if "q4_q5_rob_prf" in FILE_OVERRIDE:
        plot_rob_prf([machine])
    if "q6_levels" in FILE_OVERRIDE or "q6_levels_seq" in FILE_OVERRIDE:
        plot_levels([machine])
    if "q7_assoc" in FILE_OVERRIDE:
        plot_assoc([machine])


def main():
    if sys.argv[1:2] == ["--file"]:
        if len(sys.argv) < 3:
            sys.exit("usage: plot.py --file <csv> [<csv> ...]")
        plot_files(sys.argv[2:])
        return
    machines = []
    for arg in sys.argv[1:] or ["artemisia", "crux"]:
        m, _, run = arg.partition(":")
        if run:
            PINNED_RUN[m] = run
        machines.append(m)
    plot_rob_prf(machines)
    plot_levels(machines)
    plot_assoc(machines)


if __name__ == "__main__":
    main()

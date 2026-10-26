/*
 * q6_q7_tlb.c - Q6 (number of TLB levels and entries per level) and
 * Q7 (TLB associativity).
 *
 * Method. A randomly permuted pointer chase visits P pages, touching ONE
 * cache line per page. Each load depends on the previous one, so the time
 * per load is the full load-to-use latency. While all P translations fit in
 * the L1 dTLB, every load is an L1 dTLB hit; once P exceeds the L1 dTLB, loads
 * also pay an STLB lookup; once P exceeds the STLB, they pay a page walk.
 * Each step in latency vs P marks one TLB level and its entry count.
 *
 *   levels mode (Q6): pages spaced 1 page apart, P swept geometrically.
 *   assoc  mode (Q7): pages spaced 2^k pages apart, N swept 1..nmax for each
 *                     k. With stride >= number of sets, every page maps to the
 *                     same set (if the set index is the low VPN bits), so
 *                     misses start at N = ways instead of N = entries.
 *
 * Confounders handled here (CLAUDE.md section 7):
 *  - 4 KB pages forced with MADV_NOHUGEPAGE; THP usage is printed (expect 0).
 *  - Pages are pre-touched when the chain is written, so no page faults (and
 *    no syscalls) happen inside the timed region.
 *  - Random visiting order: no stride for the stream/IP-stride prefetchers
 *    or the next-page TLB prefetcher to learn.
 *  - The in-page line offset rotates through a random permutation of all 64
 *    line slots, so the P lines spread evenly over the L1d sets instead of
 *    piling into one set (which would add cache conflict misses at large
 *    strides). Lines are never in the same 128 B pair as another visited line.
 *  - Chase values are byte offsets from the buffer base, not raw pointers.
 *  - "control" layout (levels mode): the same number of lines with the same
 *    set distribution, but packed 64 per page, so it uses ~64x fewer pages.
 *    Any step that shows up in BOTH layouts is a data-cache effect (L1d/L2
 *    capacity); a step only in the "tlb" layout is a TLB effect.
 *
 * Usage:
 *   q6_q7_tlb levels [-p MIN:MAX] [-q points_per_octave] [-r MIN:MAX:STEP]
 *                    [--seq] [--no-control] [common options]
 *   q6_q7_tlb assoc  [-k max_log2_stride] [-N max_pages] [common options]
 *   common: [-t trials] [-l loads_per_trial] [-c cpu] [-s seed] [-o out.csv]
 */
#include "common.h"

#include <getopt.h>
#include <sys/mman.h>

#define UNROLL 16

/*
 * The timed kernel: n dependent loads, each "off = *(base + off)".
 * 16 loads are unrolled per loop iteration; the sub/jnz loop branch is
 * perfectly predictable and not on the dependency chain, so it costs
 * (almost) nothing compared with the load latency.
 */
static uint64_t chase(const char *base, uint64_t off, uint64_t n)
{
    __asm__ volatile(
        ".p2align 6\n"
        "1:\n\t"
        ".rept 16\n\t"
        "mov (%[b],%[o]), %[o]\n\t"
        ".endr\n\t"
        "sub $16, %[n]\n\t"
        "jnz 1b"
        : [o] "+r"(off), [n] "+r"(n)
        : [b] "r"(base)
        : "cc", "memory");
    return off;
}

static uint64_t line_slot[LINES_PER_PAGE];  /* random permutation of 0..63 */

/* Byte offset (from base) of the line used for element i. */
static uint64_t elem_offset(uint64_t i, uint64_t stride_pages, int control)
{
    uint64_t line = line_slot[i % LINES_PER_PAGE] * LINE_SIZE;
    if (control)                     /* 64 lines packed into each page */
        return (i / LINES_PER_PAGE) * PAGE_SIZE + line;
    return i * stride_pages * PAGE_SIZE + line;   /* one line per page */
}

/*
 * Build the chain for one point and time it.
 * Writes one CSV row per trial.
 */
static void run_point(FILE *out, char *base, const char *layout, int control,
                      int sequential, uint64_t stride, uint64_t npages,
                      int trials, uint64_t loads)
{
    uint32_t *order = malloc(npages * sizeof(uint32_t));
    double *cpl = malloc(trials * sizeof(double));
    if (!order || !cpl)
        die("malloc");

    if (sequential)
        for (uint64_t i = 0; i < npages; i++)
            order[i] = (uint32_t)i;
    else
        random_permutation(order, npages);

    /* Link element order[k] -> order[k+1] (a single cycle). Writing the
     * chain also faults in every page before timing starts. */
    for (uint64_t k = 0; k < npages; k++)
        *(uint64_t *)(base + elem_offset(order[k], stride, control)) =
            elem_offset(order[(k + 1) % npages], stride, control);
    uint64_t off = elem_offset(order[0], stride, control);

    /* Warm-up: walk the cycle at least twice to fill caches and TLBs. */
    uint64_t warm = (2 * npages + 4096 + UNROLL - 1) / UNROLL * UNROLL;
    off = chase(base, off, warm);

    double ratio = tsc_per_core_cycle();
    for (int t = 0; t < trials; t++) {
        uint64_t t0 = tsc_now();
        off = chase(base, off, loads);
        uint64_t t1 = tsc_now();
        double tpl = (double)(t1 - t0) / loads;
        cpl[t] = tpl / ratio;
        fprintf(out, "%s,%lu,%lu,%d,%lu,%lu,%.4f,%.4f,%.4f\n", layout,
                (unsigned long)stride, (unsigned long)npages, t,
                (unsigned long)(t1 - t0), (unsigned long)loads, tpl, ratio,
                cpl[t]);
    }
    fprintf(stderr, "%-7s stride=%-6lu pages=%-6lu median %.2f cycles/load\n",
            layout, (unsigned long)stride, (unsigned long)npages,
            median(cpl, trials));
    fflush(out);
    free(order);
    free(cpl);
}

static char *map_region(size_t bytes)
{
    /* MAP_NORESERVE: assoc mode reserves gigabytes of virtual space but
     * touches only the pages actually used. */
    char *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        die("mmap %zu bytes: %s", bytes, strerror(errno));
    if (madvise(p, bytes, MADV_NOHUGEPAGE) != 0)
        fprintf(stderr, "warning: MADV_NOHUGEPAGE failed: %s\n",
                strerror(errno));
    return p;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: q6_q7_tlb levels [-p MIN:MAX] [-q points_per_octave]\n"
        "                        [-r MIN:MAX:STEP] [--seq] [--no-control]\n"
        "       q6_q7_tlb assoc  [-k max_log2_stride] [-N max_pages]\n"
        "  common: [-t trials] [-l loads_per_trial] [-c cpu] [-s seed]\n"
        "          [-o out.csv]\n"
        "  -r overrides -p/-q with a linear sweep (to zoom in on a step)\n");
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        usage();
    int assoc = !strcmp(argv[1], "assoc");
    if (!assoc && strcmp(argv[1], "levels"))
        usage();

    long pmin = 4, pmax = 32768, ppo = 16;      /* levels: geometric sweep */
    long rmin = 0, rmax = 0, rstep = 0;         /* levels: linear override */
    long kmax = 14, nmax = 80;                  /* assoc */
    long loads = 1 << 16;
    int trials = 31, cpu = 2, sequential = 0, control = 1;
    const char *out_path = NULL;

    static const struct option longopts[] = {
        { "seq", no_argument, NULL, 'S' },
        { "no-control", no_argument, NULL, 'C' },
        { NULL, 0, NULL, 0 } };
    int opt;
    optind = 2;
    while ((opt = getopt_long(argc, argv, "p:q:r:k:N:t:l:c:s:o:h",
                              longopts, NULL)) != -1) {
        switch (opt) {
        case 'p':
            if (sscanf(optarg, "%ld:%ld", &pmin, &pmax) != 2 || pmin < 1 ||
                pmax < pmin)
                usage();
            break;
        case 'q': ppo = atol(optarg); break;
        case 'r': parse_range(optarg, &rmin, &rmax, &rstep); break;
        case 'k': kmax = atol(optarg); break;
        case 'N': nmax = atol(optarg); break;
        case 't': trials = atoi(optarg); break;
        case 'l': loads = atol(optarg); break;
        case 'c': cpu = atoi(optarg); break;
        case 's': rng_state = strtoull(optarg, NULL, 0); break;
        case 'o': out_path = optarg; break;
        case 'S': sequential = 1; break;
        case 'C': control = 0; break;
        default: usage();
        }
    }
    if (trials < 1 || ppo < 1 || nmax < 1 || kmax < 0 || kmax > 20 ||
        loads < UNROLL || loads % UNROLL)
        die("bad arguments (loads must be a multiple of %d)", UNROLL);
    if (rstep && rmin < 1)
        die("linear range must start at >= 1 page");

    pin_to_cpu(cpu);

    uint32_t slots[LINES_PER_PAGE];
    random_permutation(slots, LINES_PER_PAGE);
    for (unsigned i = 0; i < LINES_PER_PAGE; i++)
        line_slot[i] = slots[i];

    FILE *out = out_path ? fopen(out_path, "w") : stdout;
    if (!out)
        die("open %s: %s", out_path, strerror(errno));
    fprintf(out, "layout,stride_pages,pages,trial,ticks,loads,"
                 "ticks_per_load,tsc_per_cycle,cycles_per_load\n");

    if (!assoc) {
        /* Q6: list of page counts, geometric (default) or linear (-r). */
        long top = rstep ? rmax : pmax;
        char *base = map_region((size_t)top * PAGE_SIZE);
        long prev = 0;
        for (long k = 0;; k++) {
            long p;
            if (rstep) {
                p = rmin + k * rstep;
            } else {
                double v = pmin * __builtin_pow(2.0, (double)k / ppo);
                p = (long)(v + 0.5);
            }
            if (p > top)
                break;
            if (p == prev)              /* geometric rounding duplicates */
                continue;
            prev = p;
            run_point(out, base, sequential ? "tlb_seq" : "tlb", 0,
                      sequential, 1, p, trials, loads);
            if (control)
                run_point(out, base, "control", 1, sequential, 1, p, trials,
                          loads);
        }
        fprintf(stderr, "THP in use: %ld kB (must be 0 for 4 KB pages)\n",
                thp_anon_kb());
    } else {
        /* Q7: for each stride 2^k pages, sweep N = 1..nmax pages. */
        size_t span = (size_t)nmax * ((size_t)1 << kmax) * PAGE_SIZE;
        char *base = map_region(span);
        for (long k = 0; k <= kmax; k++)
            for (long n = 1; n <= nmax; n++)
                run_point(out, base, "tlb", 0, 0, 1UL << k, n, trials, loads);
        fprintf(stderr, "THP in use: %ld kB (must be 0 for 4 KB pages)\n",
                thp_anon_kb());
    }
    if (out != stdout)
        fclose(out);
    return 0;
}

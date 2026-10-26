/*
 * common.h - helpers shared by q4_q5_rob_prf.c and q6_q7_tlb.c
 *
 * Timing, CPU pinning, a reproducible RNG, random permutations, core-clock
 * calibration and small statistics helpers. Everything is header-only
 * (static functions) so each program is a single translation unit.
 *
 * Performance-counter policy (CLAUDE.md 5a): nothing in here reads a PMU
 * counter. All results come from the time-stamp counter.
 */
#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096UL
#define LINE_SIZE 64UL
#define LINES_PER_PAGE (PAGE_SIZE / LINE_SIZE)

#define die(...) do { fprintf(stderr, "error: " __VA_ARGS__); \
                      fprintf(stderr, "\n"); exit(1); } while (0)

/*
 * Serialized TSC read.
 * The leading lfence makes every earlier instruction finish before the read;
 * rdtscp itself waits for earlier instructions to execute; the trailing
 * lfence stops later instructions from starting before the read happens.
 * Together they keep the timed region exactly between two reads.
 */
static inline uint64_t tsc_now(void)
{
    uint32_t lo, hi, aux;
    __asm__ volatile("lfence\n\trdtscp\n\tlfence"
                     : "=a"(lo), "=d"(hi), "=c"(aux) : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

/*
 * TSC ticks per core clock cycle.
 * The TSC runs at a fixed reference rate, not at the core clock. We time a
 * chain of 4000 dependent 64-bit imul instructions (latency 3 cycles on all
 * Intel cores since Nehalem; published figure, Intel optimization manual /
 * uops.info) = 12000 core cycles. Taking the minimum of a few repetitions
 * removes interrupt noise. Dividing TSC ticks by this ratio converts to
 * core cycles even if turbo changes the core frequency between runs.
 */
static double tsc_per_core_cycle(void)
{
    double best = 1e30;
    for (int rep = 0; rep < 7; rep++) {
        uint64_t x = 3;
        uint64_t t0 = tsc_now();
        __asm__ volatile(".rept 4000\n\timul %0, %0\n\t.endr" : "+r"(x));
        uint64_t t1 = tsc_now();
        double r = (double)(t1 - t0) / (4000.0 * 3.0);
        if (r < best)
            best = r;
    }
    return best;
}

/* Pin the calling thread to one logical CPU so it never migrates. */
static void pin_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        die("sched_setaffinity(%d): %s", cpu, strerror(errno));
}

/* splitmix64: tiny, fast, reproducible RNG (seeded from the command line). */
static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

static inline uint64_t rng_next(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Uniform integer in [0, n). Modulo bias is negligible for n << 2^64. */
static inline uint64_t rng_below(uint64_t n) { return rng_next() % n; }

/* Fill a[0..n) with a uniformly random permutation of 0..n-1 (Fisher-Yates). */
static void random_permutation(uint32_t *a, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++)
        a[i] = (uint32_t)i;
    for (uint64_t i = n - 1; i > 0; i--) {
        uint64_t j = rng_below(i + 1);
        uint32_t t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

/*
 * Amount of transparent-huge-page memory mapped by this process, in kB.
 * Used to check that the TLB experiment really runs on 4 KB pages (expect 0)
 * and to report whether the ROB experiment got huge pages.
 */
static long thp_anon_kb(void)
{
    FILE *f = fopen("/proc/self/smaps_rollup", "r");
    char line[256];
    long kb = -1;
    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "AnonHugePages: %ld kB", &kb) == 1)
            break;
    fclose(f);
    return kb;
}

/* Median of n doubles (copies, so the caller's order is kept). */
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(const double *v, int n)
{
    double *c = malloc(n * sizeof(double));
    memcpy(c, v, n * sizeof(double));
    qsort(c, n, sizeof(double), cmp_double);
    double m = (n % 2) ? c[n / 2] : 0.5 * (c[n / 2 - 1] + c[n / 2]);
    free(c);
    return m;
}

/* Parse "MIN:MAX:STEP" into three integers. */
static void parse_range(const char *s, long *lo, long *hi, long *step)
{
    if (sscanf(s, "%ld:%ld:%ld", lo, hi, step) != 3 || *step <= 0 || *hi < *lo)
        die("bad range '%s' (want MIN:MAX:STEP)", s);
}

#endif /* COMMON_H */

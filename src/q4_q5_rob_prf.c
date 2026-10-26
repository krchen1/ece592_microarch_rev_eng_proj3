/*
 * q4_q5_rob_prf.c - Q4 (reorder buffer size) and Q5 (physical register file
 * size and register class).
 *
 * Method (after H. Wong, "Measuring Reorder Buffer Capacity", 2013).
 * One timed "block" is:
 *
 *     load A        A = mem[base + A]   (cache miss, pointer chase A)
 *     N fillers
 *     load B        B = mem[base + B]   (cache miss, pointer chase B)
 *     N fillers
 *
 * A and B walk two independent random chains through a buffer much larger
 * than the last-level cache, so every chase load misses to DRAM. Load A of
 * the next block depends on load A of this block, likewise for B.
 *
 *  - If load A, the N fillers and load B all fit in the out-of-order window
 *    at the same time, the two misses overlap: one block costs ~1 miss.
 *  - Once the fillers use up some back-end resource, load B cannot be
 *    allocated until load A retires: one block costs ~2 misses.
 *
 * The N where the time per block steps up is the capacity of the first
 * resource the fillers run out of. The filler type picks that resource:
 *
 *   nop    1 uop, ROB entry, no destination register      -> ROB     (Q4)
 *   gpr    add r64, rbp: ROB entry + integer register     -> int PRF (Q5)
 *   flags  cmp rcx, rbp: writes only RFLAGS               -> does a flags-only
 *                                                            write use a PRF entry?
 *   vec    vpaddd ymm, ymm15, ymm7: ROB + vector register -> vec PRF (Q5)
 *   mask   korw k, k6, k7: ROB + AVX-512 mask register    -> mask PRF (Q5)
 *   mix    alternating gpr / vec                          -> if the knee is
 *          higher than gpr or vec alone, the two classes use separate files.
 *
 * For a register class, knee ~= free physical registers, so
 * PRF size ~= knee + architectural registers held by committed state.
 *
 * Why the code is generated at run time: fillers must be straight-line (no
 * loop branch between the two loads) and N is swept over hundreds of values.
 * We emit machine code into an mmap'd buffer for each (filler, N) point.
 * Fillers never use zeroing idioms (xor r,r) or reg-reg moves, which the
 * renamer eliminates without allocating a physical register.
 *
 * Usage: q4_q5_rob_prf [-f nop,gpr,flags,vec,mask,mix|all] [-n MIN:MAX:STEP]
 *                      [-t trials] [-c cpu] [-b buffer_MB] [-l loops]
 *                      [-s seed] [-H] [-o out.csv] [-d dump.bin]
 */
#include "common.h"

#include <stdarg.h>
#include <sys/mman.h>
#include <unistd.h>

/* x86-64 register numbers as encoded in ModRM / REX / VEX. */
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
       R8, R9, R10, R11, R12, R13, R14, R15 };

#define BLOCKS_PER_LOOP 4   /* blocks unrolled per loop iteration */

/* ------------------------------------------------------------------ */
/* Tiny machine-code emitter                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *buf;
    size_t len, cap;
} code_t;

static void emit_bytes(code_t *c, const uint8_t *b, size_t n)
{
    if (c->len + n > c->cap)
        die("code buffer overflow (N too large)");
    memcpy(c->buf + c->len, b, n);
    c->len += n;
}

#define EMIT(c, ...) emit_bytes((c), (const uint8_t[]){__VA_ARGS__}, \
                                sizeof((const uint8_t[]){__VA_ARGS__}))

/* "op r/m64, r64" with both operands registers: add (0x01), cmp (0x39),
 * mov (0x89). REX.W=1, REX.R extends src, REX.B extends dst. */
static void emit_rr(code_t *c, uint8_t op, int dst, int src)
{
    EMIT(c, 0x48 | ((src >> 3) << 2) | (dst >> 3), op,
         0xC0 | ((src & 7) << 3) | (dst & 7));
}

/* mov reg, [r15 + reg] : one pointer-chase step (r15 = buffer base). */
static void emit_chase(code_t *c, int reg)
{
    /* REX.W + R(reg) + X(index=reg) + B(base=r15); ModRM rm=100 -> SIB. */
    EMIT(c, 0x48 | ((reg >> 3) << 2) | ((reg >> 3) << 1) | 1, 0x8B,
         ((reg & 7) << 3) | 4, ((reg & 7) << 3) | (R15 & 7));
}

/* vpaddd ymm(dst), ymm15, ymm7 : 2-byte VEX, L=256, pp=66, map 0F. */
static void emit_vpaddd(code_t *c, int dst)
{
    uint8_t r_inv = ((~dst >> 3) & 1) << 7;      /* VEX.R is inverted     */
    uint8_t vvvv  = ((~15) & 0xF) << 3;          /* second source = ymm15 */
    EMIT(c, 0xC5, r_inv | vvvv | 0x04 | 0x01, 0xFE,
         0xC0 | ((dst & 7) << 3) | 7);           /* third source = ymm7   */
}

/* korw k(dst), k6, k7 : VEX.L1.0F.W0 45 /r (AVX-512F). */
static void emit_korw(code_t *c, int dst)
{
    uint8_t vvvv = ((~6) & 0xF) << 3;            /* second source = k6    */
    EMIT(c, 0xC5, 0x80 | vvvv | 0x04, 0x45,
         0xC0 | ((dst & 7) << 3) | 7);           /* third source = k7     */
}

/* ------------------------------------------------------------------ */
/* Filler types                                                       */
/* ------------------------------------------------------------------ */

enum filler { F_NOP, F_GPR, F_FLAGS, F_VEC, F_MASK, F_MIX, F_COUNT };
static const char *filler_name[F_COUNT] = {
    "nop", "gpr", "flags", "vec", "mask", "mix" };

/* Destination registers are rotated so no single register gets a long
 * dependency chain. rax/rbx (chases), r15 (base), r14 (state pointer),
 * r12 (loop counter), rbp (constant source) and rsp are never written. */
static const int gpr_dst[] = { RCX, RDX, RSI, RDI, R8, R9, R10, R11, R13 };
static const int vec_dst[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14 };
static const int k_dst[]   = { 1, 2, 3, 4, 5 };
#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Emit filler number i (i counts across the whole block). */
static void emit_filler(code_t *c, enum filler f, long i)
{
    switch (f) {
    case F_NOP:   EMIT(c, 0x90); break;
    case F_GPR:   emit_rr(c, 0x01, gpr_dst[i % NELEM(gpr_dst)], RBP); break;
    case F_FLAGS: emit_rr(c, 0x39, RCX, RBP); break;
    case F_VEC:   emit_vpaddd(c, vec_dst[i % NELEM(vec_dst)]); break;
    case F_MASK:  emit_korw(c, k_dst[i % NELEM(k_dst)]); break;
    case F_MIX:
        if (i % 2 == 0)
            emit_rr(c, 0x01, gpr_dst[(i / 2) % NELEM(gpr_dst)], RBP);
        else
            emit_vpaddd(c, vec_dst[(i / 2) % NELEM(vec_dst)]);
        break;
    default: die("bad filler");
    }
}

/*
 * Generated function:  void fn(char *base, uint64_t state[2], uint64_t loops)
 * state[0], state[1] hold the current byte offsets of chase A and chase B,
 * so successive calls continue the chains instead of restarting them.
 */
typedef void (*kernel_fn)(char *base, uint64_t *state, uint64_t loops);

static void build_kernel(code_t *c, enum filler f, long n)
{
    c->len = 0;
    /* prologue: save callee-saved registers we use */
    EMIT(c, 0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57);
    emit_rr(c, 0x89, R15, RDI);                  /* mov r15, rdi  (base)    */
    emit_rr(c, 0x89, R14, RSI);                  /* mov r14, rsi  (state)   */
    emit_rr(c, 0x89, R12, RDX);                  /* mov r12, rdx  (loops)   */
    EMIT(c, 0x49, 0x8B, 0x06);                   /* mov rax, [r14]          */
    EMIT(c, 0x49, 0x8B, 0x5E, 0x08);             /* mov rbx, [r14+8]        */
    EMIT(c, 0xBD, 0x01, 0x00, 0x00, 0x00);       /* mov ebp, 1              */

    while (c->len % 64)                          /* align loop top (run once) */
        EMIT(c, 0x90);
    size_t top = c->len;

    for (int blk = 0; blk < BLOCKS_PER_LOOP; blk++) {
        long i = 0;
        emit_chase(c, RAX);                      /* miss A */
        for (long k = 0; k < n; k++)
            emit_filler(c, f, i++);
        emit_chase(c, RBX);                      /* miss B */
        for (long k = 0; k < n; k++)
            emit_filler(c, f, i++);
    }

    /* dec r12 ; jnz top  -- one perfectly predictable branch per 4 blocks */
    EMIT(c, 0x49, 0xFF, 0xCC);
    int32_t rel = (int32_t)(top - (c->len + 6));
    EMIT(c, 0x0F, 0x85, rel & 0xFF, (rel >> 8) & 0xFF,
         (rel >> 16) & 0xFF, (rel >> 24) & 0xFF);

    EMIT(c, 0x49, 0x89, 0x06);                   /* mov [r14], rax          */
    EMIT(c, 0x49, 0x89, 0x5E, 0x08);             /* mov [r14+8], rbx        */
    if (f == F_VEC || f == F_MIX)
        EMIT(c, 0xC5, 0xF8, 0x77);               /* vzeroupper              */
    EMIT(c, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5D, 0x5B);
    EMIT(c, 0xC3);                               /* ret                     */
}

/* ------------------------------------------------------------------ */
/* Pointer-chase buffer                                               */
/* ------------------------------------------------------------------ */

/*
 * One random cycle through every cache line of the buffer. Each line holds
 * the byte offset of the next line (an index, not a raw pointer, so nothing
 * in memory looks like a pointer to a data-dependent prefetcher). Random
 * order defeats the stream / stride / adjacent-line prefetchers: the next
 * address is unknown until the load returns. Chase B starts half-way round
 * the cycle, so A and B never touch the same lines.
 */
static char *build_chase(size_t bytes, int use_huge, uint64_t state[2])
{
    char *base = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        die("mmap %zu bytes: %s", bytes, strerror(errno));
    /* Huge pages here only reduce TLB misses on the chase loads; they do not
     * change where the knee is. -H turns them off. */
    madvise(base, bytes, use_huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);

    uint64_t lines = bytes / LINE_SIZE;
    uint32_t *order = malloc(lines * sizeof(uint32_t));
    if (!order)
        die("malloc");
    random_permutation(order, lines);
    for (uint64_t k = 0; k < lines; k++)
        *(uint64_t *)(base + order[k] * LINE_SIZE) =
            order[(k + 1) % lines] * LINE_SIZE;
    state[0] = order[0] * LINE_SIZE;
    state[1] = order[lines / 2] * LINE_SIZE;
    free(order);
    return base;
}

/* ------------------------------------------------------------------ */

static void usage(void)
{
    fprintf(stderr,
        "usage: q4_q5_rob_prf [-f nop,gpr,flags,vec,mask,mix|all]\n"
        "        [-n MIN:MAX:STEP] [-t trials] [-c cpu] [-b buffer_MB]\n"
        "        [-l loops] [-s seed] [-H] [-o out.csv] [-d dump.bin]\n"
        "  -H        do not request huge pages for the chase buffer\n"
        "  -d FILE   write the generated code for the first point to FILE\n"
        "            (check with: objdump -D -b binary -mi386:x86-64 FILE)\n");
    exit(1);
}

int main(int argc, char **argv)
{
    const char *fillers = "all", *out_path = NULL, *dump_path = NULL;
    long nmin = 0, nmax = 800, nstep = 4;
    int trials = 31, cpu = 2, use_huge = 1;
    long buf_mb = 256, loops = 256;
    int opt;

    while ((opt = getopt(argc, argv, "f:n:t:c:b:l:s:Ho:d:h")) != -1) {
        switch (opt) {
        case 'f': fillers = optarg; break;
        case 'n': parse_range(optarg, &nmin, &nmax, &nstep); break;
        case 't': trials = atoi(optarg); break;
        case 'c': cpu = atoi(optarg); break;
        case 'b': buf_mb = atol(optarg); break;
        case 'l': loops = atol(optarg); break;
        case 's': rng_state = strtoull(optarg, NULL, 0); break;
        case 'H': use_huge = 0; break;
        case 'o': out_path = optarg; break;
        case 'd': dump_path = optarg; break;
        default: usage();
        }
    }
    if (trials < 1 || loops < 1 || buf_mb < 1)
        usage();

    /* Which filler types to run, skipping ones this CPU cannot execute. */
    int want[F_COUNT] = {0};
    for (int f = 0; f < F_COUNT; f++)
        if (!strcmp(fillers, "all") || strstr(fillers, filler_name[f]))
            want[f] = 1;
    __builtin_cpu_init();
    /* (a dump only writes code, never runs it, so it skips these checks) */
    if (!dump_path && (want[F_VEC] || want[F_MIX]) &&
        !__builtin_cpu_supports("avx2")) {
        fprintf(stderr, "note: no AVX2 -> skipping vec and mix\n");
        want[F_VEC] = want[F_MIX] = 0;
    }
    if (!dump_path && want[F_MASK] && !__builtin_cpu_supports("avx512f")) {
        fprintf(stderr, "note: no AVX-512F -> skipping mask (unsupported "
                        "feature, record in report)\n");
        want[F_MASK] = 0;
    }

    /* Pin first, so the buffer is first-touched on this core's NUMA node. */
    pin_to_cpu(cpu);

    uint64_t state[2];
    size_t bytes = (size_t)buf_mb << 20;
    char *base = build_chase(bytes, use_huge, state);
    fprintf(stderr, "chase buffer %ld MB, THP in use: %ld kB\n",
            buf_mb, thp_anon_kb());

    code_t code;
    code.cap = (size_t)BLOCKS_PER_LOOP * 2 * (8 + nmax * 4) + 4096;
    code.cap = (code.cap + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    code.buf = mmap(NULL, code.cap, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code.buf == MAP_FAILED)
        die("mmap code: %s", strerror(errno));

    FILE *out = out_path ? fopen(out_path, "w") : stdout;
    if (!out)
        die("open %s: %s", out_path, strerror(errno));
    fprintf(out, "filler,n,trial,ticks,blocks,ticks_per_block,"
                 "tsc_per_cycle,cycles_per_block\n");

    double *cpb = malloc(trials * sizeof(double));
    uint64_t blocks = (uint64_t)loops * BLOCKS_PER_LOOP;

    for (int f = 0; f < F_COUNT; f++) {
        if (!want[f])
            continue;
        for (long n = nmin; n <= nmax; n += nstep) {
            /* write the code while the buffer is RW, then make it RX */
            mprotect(code.buf, code.cap, PROT_READ | PROT_WRITE);
            build_kernel(&code, f, n);
            if (dump_path) {
                FILE *d = fopen(dump_path, "wb");
                if (!d || fwrite(code.buf, 1, code.len, d) != code.len)
                    die("dump %s", dump_path);
                fclose(d);
                fprintf(stderr, "dumped %s N=%ld (%zu bytes) to %s\n",
                        filler_name[f], n, code.len, dump_path);
                return 0;
            }
            if (mprotect(code.buf, code.cap, PROT_READ | PROT_EXEC) != 0)
                die("mprotect RX: %s", strerror(errno));
            kernel_fn fn = (kernel_fn)(void *)code.buf;

            /* warm-up: code into i-cache/uop cache, predictor trained */
            fn(base, state, loops);
            fn(base, state, loops);

            double ratio = tsc_per_core_cycle();
            for (int t = 0; t < trials; t++) {
                uint64_t t0 = tsc_now();
                fn(base, state, loops);
                uint64_t t1 = tsc_now();
                double tpb = (double)(t1 - t0) / blocks;
                cpb[t] = tpb / ratio;
                fprintf(out, "%s,%ld,%d,%lu,%lu,%.3f,%.4f,%.3f\n",
                        filler_name[f], n, t, (unsigned long)(t1 - t0),
                        (unsigned long)blocks, tpb, ratio, cpb[t]);
            }
            fprintf(stderr, "%-5s N=%4ld  median %.1f cycles/block\n",
                    filler_name[f], n, median(cpb, trials));
        }
    }
    if (out != stdout)
        fclose(out);
    return 0;
}

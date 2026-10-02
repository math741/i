/* MLG probe: mede o silício e julga candidatos. Compilado sem -ffast-math. */
#define _GNU_SOURCE
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* ---------- largura de banda de memória (triad, convenção STREAM: 24 B/iter) ---------- */
typedef struct { double *a, *b, *c; long lo, hi; int reps; } bw_job;

static void *bw_worker(void *p) {
    bw_job *j = (bw_job *)p;
    for (int r = 0; r < j->reps; r++) {
        const double s = 1.0 + r * 1e-9;
        for (long i = j->lo; i < j->hi; i++) j->a[i] = j->b[i] + s * j->c[i];
    }
    return NULL;
}

double mlg_bandwidth(long n, int threads, int reps) {
    double *a = malloc(n * sizeof(double)), *b = malloc(n * sizeof(double)), *c = malloc(n * sizeof(double));
    if (!a || !b || !c) { free(a); free(b); free(c); return -1; }
    for (long i = 0; i < n; i++) { a[i] = 0; b[i] = 1.0; c[i] = 2.0; }
    if (threads < 1) threads = 1;
    if (threads > 64) threads = 64;
    pthread_t th[64];
    bw_job jb[64];
    double best = 1e30;
    for (int pass = 0; pass < 3; pass++) {
        double t0 = now();
        for (int t = 0; t < threads; t++) {
            jb[t] = (bw_job){a, b, c, n * t / threads, n * (t + 1) / threads, reps};
            if (t) pthread_create(&th[t], NULL, bw_worker, &jb[t]);
        }
        bw_worker(&jb[0]);
        for (int t = 1; t < threads; t++) pthread_join(th[t], NULL);
        double dt = now() - t0;
        if (dt < best) best = dt;
    }
    volatile double sink = a[n / 2];
    (void)sink;
    free(a); free(b); free(c);
    return 24.0 * n * reps / best;
}

/* ---------- dados ---------- */
void mlg_fill(float *x, long n, uint32_t seed) {
    uint64_t s = seed * 2654435761u + 1;
    for (long i = 0; i < n; i++) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        x[i] = (float)((double)(s >> 40) / (double)(1ULL << 24) * 2.0 - 1.0);
    }
}

void mlg_poison(float *x, long n) {
    for (long i = 0; i < n; i++) x[i] = NAN;
}

/* ---------- gate de verificação ----------
 * Referência em double: R = A·B e S = |A|·|B|.
 * Teorema (Higham, Accuracy and Stability of Numerical Algorithms, §3.1):
 * qualquer ordem de soma em ponto flutuante de K produtos satisfaz
 *     |Ĉ_ij − C_ij| ≤ γ_K · (|A|·|B|)_ij,  γ_K = K·u / (1 − K·u),  u = 2^-24.
 * Ou seja: QUALQUER reordenação/tiling/vetorização/FMA correta cabe nesse limite.
 * Código que viola o limite não é "um pouco impreciso": está provadamente errado. */
void mlg_ref(int M, int N, int K, const float *A, const float *B, double *R, double *S) {
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) {
            double s = 0, sa = 0;
            for (int k = 0; k < K; k++) {
                double p = (double)A[(long)i * K + k] * (double)B[(long)k * N + j];
                s += p;
                sa += fabs(p);
            }
            R[(long)i * N + j] = s;
            S[(long)i * N + j] = sa;
        }
}

/* Retorna max(erro / limite_provado). <= 1 passa; > 1 é prova de bug. */
double mlg_check(int M, int N, int K, const float *C, const double *R, const double *S) {
    const double u = FLT_EPSILON / 2;
    const double g = K * u / (1 - K * u) + 2 * K * DBL_EPSILON; /* + folga do próprio double */
    double worst = 0;
    for (long i = 0; i < (long)M * N; i++) {
        double c = C[i];
        if (!isfinite(c)) return INFINITY;
        double err = fabs(c - R[i]), bound = g * S[i];
        double ratio = bound > 0 ? err / bound : (err > 0 ? INFINITY : 0);
        if (ratio > worst) worst = ratio;
    }
    return worst;
}

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

void mlg_fill_scaled(float *x, long n, uint32_t seed, float scale) {
    mlg_fill(x, n, seed);
    for (long i = 0; i < n; i++) x[i] *= scale; /* scale = potência de 2: continua exato */
}

/* ================================ RAIZ DE CONFIANÇA ================================
 * Para cada operação a raiz fornece: referência em double + TOLERÂNCIA por elemento
 * derivada de uma análise de erro. O comparador é um só (mlg_check_tol).
 * Violar a tolerância é CONTRAEXEMPLO (prova de bug, dado o contrato).
 * Passar é evidência E2 (diferencial), não prova: provar que o binário implementa o
 * algoritmo analisado é o que Rice impede em geral.
 * ================================================================================== */

/* ---------- GEMM: C = A·B ----------
 * Teorema (Higham, Accuracy and Stability of Numerical Algorithms, 2ª ed., §3.1): sob
 * arredondamento ao mais próximo e sem underflow/overflow, QUALQUER ordem de avaliação do
 * produto interno de K termos satisfaz |Ĉ_ij − C_ij| ≤ γ_K·(|A|·|B|)_ij, γ_K = Ku/(1−Ku).
 * Hipóteses garantidas pelo domínio: entradas múltiplas de 2^-23 em [-1,1] => todo valor
 * intermediário (com ou sem FMA) é múltiplo de 2^-69, nunca subnormal; |soma| ≤ K. */
void mlg_gemm_ref(int M, int N, int K, const float *A, const float *B, double *R, double *T) {
    const double u = FLT_EPSILON / 2;
    const double g = K * u / (1 - K * u) + 2 * K * DBL_EPSILON; /* + erro do próprio double */
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) {
            double s = 0, sa = 0;
            for (int k = 0; k < K; k++) {
                double p = (double)A[(long)i * K + k] * (double)B[(long)k * N + j];
                s += p;
                sa += fabs(p);
            }
            R[(long)i * N + j] = s;
            T[(long)i * N + j] = g * sa;
        }
}

/* ---------- ATENÇÃO: Y = softmax(Q·Kᵀ/√D)·V  (Q, K, V: S×D) ----------
 * Semântica: a identidade do softmax online (máximo corrente m, normalizador l, saída o com
 * reescala por exp(m_velho − m_novo)) é exata em ℝ; isso é teorema, vale para qualquer
 * blocagem. A tolerância numérica abaixo é uma análise de PRIMEIRA ORDEM (não teorema):
 *   erro no expoente de cada peso: γ_D·(|q|·|k|)/√D + 2u|s| + u·R   (R = m − min s)
 *   exp: ≤ 8 ulp = 16u (HIPÓTESE sobre a libm/libmvec, registrada no contrato)
 *   reescalas do softmax online: ≤ S−1, cada uma com ≤ 16u + 2u; a soma dos |Δm| é ≤ R
 *   normalização e acumulação P·V: 2γ_S + 2u
 *   |Ŷ − Y| ≤ 2·(2η + 2γ_S + 2u)·Σ_j p_j|v_j|   (fator 2 cobre termos de 2ª ordem)
 * Underflow de exp(s − m) é benigno: o termo do máximo vale 1, então o erro absoluto ≤ S·2^-126.
 * Overflow é impossível SE o máximo for subtraído — é exatamente isso que o caso de
 * logits grandes do gate testa. */
void mlg_attn_ref(int S, int D, const float *Q, const float *K, const float *V, double *R, double *T) {
    const double u = FLT_EPSILON / 2, sc = 1.0 / sqrt((double)D);
    const double gD = D * u / (1 - D * u), gS = S * u / (1 - S * u), eexp = 16 * u;
    double *s = malloc(S * sizeof(double)), *a = malloc(S * sizeof(double));
    for (int i = 0; i < S; i++) {
        double m = -INFINITY, mn = INFINITY;
        for (int j = 0; j < S; j++) {
            double d = 0, da = 0;
            for (int c = 0; c < D; c++) {
                double p = (double)Q[(long)i * D + c] * (double)K[(long)j * D + c];
                d += p;
                da += fabs(p);
            }
            s[j] = d * sc;
            a[j] = da * sc;
            if (s[j] > m) m = s[j];
            if (s[j] < mn) mn = s[j];
        }
        const double Rg = m - mn;
        double eta = 0;
        for (int j = 0; j < S; j++) {
            double e = gD * a[j] + 2 * u * fabs(s[j]);
            if (e > eta) eta = e;
        }
        eta += u * Rg + eexp + (S - 1) * (eexp + 2 * u) + u * Rg;
        double sum = 0;
        for (int j = 0; j < S; j++) {
            s[j] = exp(s[j] - m);
            sum += s[j];
        }
        for (int c = 0; c < D; c++) {
            double y = 0, ya = 0;
            for (int j = 0; j < S; j++) {
                double p = s[j] / sum, v = V[(long)j * D + c];
                y += p * v;
                ya += p * fabs(v);
            }
            R[(long)i * D + c] = y;
            T[(long)i * D + c] = 2 * (2 * eta + 2 * gS + 2 * u) * ya + 1e-300;
        }
    }
    free(s);
    free(a);
}

/* ---------- comparador único: max(|out − ref| / tol). <= 1 passa; > 1 é contraexemplo ---------- */
double mlg_check_tol(long n, const float *out, const double *R, const double *T) {
    double worst = 0;
    for (long i = 0; i < n; i++) {
        double c = out[i];
        if (!isfinite(c)) return INFINITY;
        double ratio = fabs(c - R[i]) / T[i];
        if (ratio > worst) worst = ratio;
    }
    return worst;
}

/* ---------- ambiente de ponto flutuante, medido (não presumido) ---------- */
#include <fenv.h>

/* out[0]=modo de arredondamento (0 nearest,1 down,2 up,3 zero,-1 ?), out[1]=FTZ, out[2]=DAZ */
void mlg_fp_env(int *out) {
    int r = fegetround();
    out[0] = r == FE_TONEAREST ? 0 : r == FE_DOWNWARD ? 1 : r == FE_UPWARD ? 2 : r == FE_TOWARDZERO ? 3 : -1;
    volatile float tiny = 1e-38f, half = 0.5f, denorm = 1e-40f, two = 2.0f;
    out[1] = (tiny * half) == 0.0f;  /* resultado subnormal virou zero? */
    out[2] = (denorm * two) == 0.0f; /* entrada subnormal tratada como zero? */
}

/* ---------- frequência efetiva sob carga: cadeia de somas dependentes (1 ciclo cada) ---------- */
typedef struct { long iters; double hz; } fq_job;

static void *fq_worker(void *p) {
    fq_job *j = (fq_job *)p;
    unsigned long x = 1;
    double t0 = now();
    for (long i = 0; i < j->iters; i++) {
#define STEP x += (unsigned long)i; __asm__ volatile("" : "+r"(x));
        STEP STEP STEP STEP STEP STEP STEP STEP
    }
    double dt = now() - t0;
    volatile unsigned long sink = x;
    (void)sink;
    j->hz = 8.0 * j->iters / dt;
    return NULL;
}

/* Estimativa (limite inferior) da frequência média por thread com `threads` threads ativas. */
double mlg_freq(long iters, int threads) {
    if (threads < 1) threads = 1;
    if (threads > 64) threads = 64;
    pthread_t th[64];
    fq_job jb[64];
    for (int t = 0; t < threads; t++) {
        jb[t] = (fq_job){iters, 0};
        if (t) pthread_create(&th[t], NULL, fq_worker, &jb[t]);
    }
    fq_worker(&jb[0]);
    for (int t = 1; t < threads; t++) pthread_join(th[t], NULL);
    double s = 0;
    for (int t = 0; t < threads; t++) s += jb[t].hz;
    return s / threads;
}

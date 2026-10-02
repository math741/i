/* MLG peak: teto de FLOP/s medido. Compilado com -ffast-math para permitir SIMD
 * em todo alvo (no ARMv7 o NEON só é usado para float com essa permissão). */
#include <pthread.h>
#include <time.h>

#define ACC 64

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

typedef struct { long iters; float out; } fl_job;

static void *fl_worker(void *p) {
    fl_job *j = (fl_job *)p;
    volatile float vx = 0.9999999f, vy = 1e-7f;
    const float x = vx, y = vy;
    float acc[ACC];
    for (int l = 0; l < ACC; l++) acc[l] = l * 1e-3f;
    for (long i = 0; i < j->iters; i++)
        for (int l = 0; l < ACC; l++) acc[l] = acc[l] * x + y;
    float s = 0;
    for (int l = 0; l < ACC; l++) s += acc[l];
    j->out = s;
    return NULL;
}

/* FLOP/s com ACC acumuladores independentes por thread (2 FLOP por FMA). */
double mlg_peak_flops(long iters, int threads) {
    if (threads < 1) threads = 1;
    if (threads > 64) threads = 64;
    pthread_t th[64];
    fl_job jb[64];
    double t0 = now();
    for (int t = 0; t < threads; t++) {
        jb[t] = (fl_job){iters, 0};
        if (t) pthread_create(&th[t], NULL, fl_worker, &jb[t]);
    }
    fl_worker(&jb[0]);
    for (int t = 1; t < threads; t++) pthread_join(th[t], NULL);
    double dt = now() - t0;
    volatile float sink = jb[0].out;
    (void)sink;
    return 2.0 * ACC * iters * threads / dt;
}

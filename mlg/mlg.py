#!/usr/bin/env python3
"""MLG semente: o laço central rodando em silício de verdade.

    hardware real -> DNA medido -> síntese -> gate (contrato + limite provado)
    -> benchmark estatístico -> seleção -> duelo com o campeão -> relatório bruto

Todo número que aparece na tela também vai, com as amostras cruas, para
reports/<máquina>-<data>.json, para ser auditado.
Só depende de python3 e de um compilador C (cc/gcc/clang). Roda no Pi Zero 2 W.
"""
import argparse
import concurrent.futures
import ctypes
import glob
import hashlib
import json
import math
import os
import platform
import random
import re
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, ".build")
REPORTS = os.path.join(HERE, "reports")
STATE = os.path.join(HERE, "mlg_state.json")
CC = os.environ.get("CC", "cc")

F32P = ctypes.POINTER(ctypes.c_float)
F64P = ctypes.POINTER(ctypes.c_double)

# Níveis de evidência de correção (do mais forte ao mais fraco).
LEVELS = {4: "FORMAL", 3: "CERTIFIED", 2: "DIFFERENTIAL+BOUND", 1: "PROPERTY", 0: "EXPERIMENTAL"}
PROMOTION_MIN_LEVEL = 2


# --------------------------------------------------------------------------- compilação

def compile_so(sources, out, flags):
    cmd = [CC] + flags + ["-shared", "-fPIC", "-o", out] + sources + ["-lpthread", "-lm"]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    return r.returncode == 0, r.stderr


def detect_flags():
    """Descobre o conjunto de flags mais agressivo que este compilador aceita aqui."""
    os.makedirs(BUILD, exist_ok=True)
    src = os.path.join(BUILD, "flagtest.c")
    with open(src, "w") as f:
        f.write("float f(float*a,int n){float s=0;for(int i=0;i<n;i++)s+=a[i]*a[i];return s;}\n")
    for flags in (["-O3", "-march=native"], ["-O3", "-mcpu=native", "-mfpu=auto"],
                  ["-O3", "-mcpu=native"], ["-O3"], ["-O2"]):
        ok, _ = compile_so([src], os.path.join(BUILD, "flagtest.so"), flags)
        if ok:
            return flags
    sys.exit("nenhum compilador C funcional encontrado (defina CC=...)")


def isa_variants(flags):
    """Escolhas de ISA que entram no espaço de busca (e no probe do teto)."""
    variants = {"default": []}
    if "avx512f" in cpu_flags():
        extra = ["-mprefer-vector-width=512"]
        ok, _ = compile_so([os.path.join(BUILD, "flagtest.c")], os.path.join(BUILD, "flagtest.so"), flags + extra)
        if ok:
            variants["vw512"] = extra
    return variants


def compiler_version():
    r = subprocess.run([CC, "--version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    return (r.stdout.splitlines() or ["?"])[0]


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


FMA_RE = re.compile(r"\b(v?fn?m(add|sub)\d*[a-z]*|fmla|fmls|vfma|vfms)\b")


def count_fma(so):
    """Conta instruções FMA no binário (None se não houver objdump)."""
    try:
        r = subprocess.run(["objdump", "-d", so], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           universal_newlines=True)
    except OSError:
        return None
    return len(FMA_RE.findall(r.stdout)) if r.returncode == 0 else None


# --------------------------------------------------------------------------- DNA do hardware

def read_first(path, default=None):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return default


def cpu_model():
    info = read_first("/proc/cpuinfo", "")
    for key in ("model name", "Model", "Hardware", "cpu model"):
        for line in info.splitlines():
            if line.split(":")[0].strip() == key:
                return line.split(":", 1)[1].strip()
    return platform.processor() or platform.machine()


def cpu_flags():
    for line in (read_first("/proc/cpuinfo", "") or "").splitlines():
        if line.split(":")[0].strip() in ("flags", "Features"):
            return set(line.split(":", 1)[1].split())
    return set()


def temperature():
    temps = []
    for p in glob.glob("/sys/class/thermal/thermal_zone*/temp"):
        v = read_first(p)
        if v and v.strip().lstrip("-").isdigit():
            temps.append(int(v) / 1000.0)
    return max(temps) if temps else None


def meminfo_mb(key):
    for line in (read_first("/proc/meminfo", "") or "").splitlines():
        if line.startswith(key + ":"):
            return int(line.split()[1]) // 1024
    return None


def last_level_cache():
    """(nível, bytes) do maior cache visto pela cpu0."""
    best = (0, 0)
    for d in glob.glob("/sys/devices/system/cpu/cpu0/cache/index*"):
        lvl, size = read_first(d + "/level"), read_first(d + "/size")
        if not lvl or not size:
            continue
        size = size.strip()
        mult = {"K": 1024, "M": 1024 ** 2, "G": 1024 ** 3}.get(size[-1], 1)
        nbytes = int(size.rstrip("KMG")) * mult
        if (int(lvl), nbytes) > best:
            best = (int(lvl), nbytes)
    return best


def calibrate(fn, *args, target=0.15, start=20000):
    """Aumenta iters até a medição durar `target` s; devolve o melhor de 3."""
    iters = start
    while True:
        t0 = time.perf_counter()
        fn(iters, *args)
        if time.perf_counter() - t0 > target:
            break
        iters *= 4
    return [fn(iters, *args) for _ in range(3)]


def probe_hardware(probe, flags, variants, cpus, args):
    for name, res, argt in (("mlg_bandwidth", ctypes.c_double, [ctypes.c_long, ctypes.c_int, ctypes.c_int]),
                            ("mlg_freq", ctypes.c_double, [ctypes.c_long, ctypes.c_int])):
        getattr(probe, name).restype = res
        getattr(probe, name).argtypes = argt
    dna = {"temp_c_before": temperature()}

    # Teto de cálculo: o próprio probe é uma busca (acumuladores x largura de vetor).
    jobs = [(acc, vn) for acc in (32, 64, 128, 256) for vn in variants]

    def build(job):
        acc, vn = job
        so = os.path.join(BUILD, "peak_%d_%s.so" % (acc, vn))
        ok, err = compile_so([os.path.join(HERE, "peak.c")], so,
                             flags + variants[vn] + ["-ffast-math", "-DACC=%d" % acc])
        return job, so if ok else None

    with concurrent.futures.ThreadPoolExecutor(max_workers=cpus) as ex:
        built = dict(ex.map(build, jobs))
    peaks = []
    for (acc, vn), so in sorted(built.items()):
        if not so:
            continue
        fn = ctypes.CDLL(so).mlg_peak_flops
        fn.restype = ctypes.c_double
        fn.argtypes = [ctypes.c_long, ctypes.c_int]
        s1 = calibrate(fn, 1, start=200000 // acc)
        sn = calibrate(fn, cpus, start=200000 // acc)
        peaks.append(dict(acc=acc, isa=vn, samples_1t=s1, samples_all=sn, fma_instr=count_fma(so)))
    dna["peak_variants"] = peaks
    dna["peak_1t"] = max(max(p["samples_1t"]) for p in peaks)
    dna["peak_all"] = max(max(p["samples_all"]) for p in peaks)
    dna["peak_from"] = max(peaks, key=lambda p: max(p["samples_all"]))
    dna["peak_from"] = {"acc": dna["peak_from"]["acc"], "isa": dna["peak_from"]["isa"]}

    dna["freq_1t_hz"] = max(calibrate(probe.mlg_freq, 1, start=100000))
    dna["freq_all_hz"] = max(calibrate(probe.mlg_freq, cpus, start=100000))

    # Banda: conjunto de trabalho >= 4x o último cache, senão mede cache e não DRAM.
    lvl, llc = last_level_cache()
    avail = (meminfo_mb("MemAvailable") or 512) * 1024 ** 2
    ws = args.bw_mb * 1024 ** 2 if args.bw_mb else min(max(4 * llc, 256 * 1024 ** 2), avail // 3)
    n = int(ws / 24)
    reps = max(1, int(1.5e9 / ws))
    dna.update(llc_level=lvl, llc_bytes=llc, bw_working_set=ws,
               bw_dram_ws_over_llc=(ws / llc) if llc else None,
               bw_1t=probe.mlg_bandwidth(n, 1, reps), bw_all=probe.mlg_bandwidth(n, cpus, reps),
               bw_convention="STREAM triad, 24 B/iter (sem write-allocate: tráfego real ~32 B/iter)")
    if llc:
        nc = int(llc / 2 / 24)
        dna["bw_llc_all"] = probe.mlg_bandwidth(nc, cpus, max(1, int(3e9 / (llc / 2))))
    dna["temp_c_after"] = temperature()
    return dna


def fp_env(probe):
    out = (ctypes.c_int * 3)()
    probe.mlg_fp_env(out)
    return {"rounding": {0: "nearest", 1: "down", 2: "up", 3: "zero"}.get(out[0], "?"),
            "ftz": bool(out[1]), "daz": bool(out[2])}


COMMON = r"""
#include <pthread.h>
#define THREADS %(T)d
#define MR %(MR)d
typedef struct { int M, N, K; const float *A, *B; float *C; int r0, r1; } job_t;
static void rows(int M, int N, int K, const float *restrict A, const float *restrict B,
                 float *restrict C, int r0, int r1);
static void *run(void *p) {
    job_t *j = (job_t *)p;
    rows(j->M, j->N, j->K, j->A, j->B, j->C, j->r0, j->r1);
    return 0;
}
void mlg_kernel(int M, int N, int K, const float *A, const float *B, float *C) {
    int T = THREADS < M ? THREADS : M;
    if (T <= 1) { rows(M, N, K, A, B, C, 0, M); return; }
    pthread_t th[64]; job_t jb[64]; int b[65];
    for (int t = 0; t < T; t++) b[t] = (int)((long)M * t / T / MR * MR);
    b[T] = M;
    for (int t = 0; t < T; t++) {
        jb[t] = (job_t){M, N, K, A, B, C, b[t], b[t + 1]};
        if (t) pthread_create(&th[t], 0, run, &jb[t]);
    }
    run(&jb[0]);
    for (int t = 1; t < T; t++) pthread_join(th[t], 0);
}
"""

NAIVE = r"""
static void rows(int M, int N, int K, const float *restrict A, const float *restrict B,
                 float *restrict C, int r0, int r1) {
    for (int i = r0; i < r1; i++)
        for (int j = 0; j < N; j++) {
            float s = 0.f;
            for (int k = 0; k < K; k++) s += A[(long)i * K + k] * B[(long)k * N + j];
            C[(long)i * N + j] = s;
        }
}
"""

IKJ = r"""
static void rows(int M, int N, int K, const float *restrict A, const float *restrict B,
                 float *restrict C, int r0, int r1) {
    for (int i = r0; i < r1; i++) {
        for (int j = 0; j < N; j++) C[(long)i * N + j] = 0.f;
        for (int k = 0; k < K; k++) {
            const float av = A[(long)i * K + k];
            for (int j = 0; j < N; j++) C[(long)i * N + j] += av * B[(long)k * N + j];
        }
    }
}
"""

# Micro-kernel com bloco de registradores MR x NR e tiling de K (KC).
BLOCK = r"""
#define NR %(NR)d
#define KCV %(KC)d
static void rows(int M, int N, int K, const float *restrict A, const float *restrict B,
                 float *restrict C, int r0, int r1) {
    const int kc = KCV > 0 ? KCV : (K > 0 ? K : 1);
    for (int i = r0; i < r1; i++)
        for (int j = 0; j < N; j++) C[(long)i * N + j] = 0.f;
    for (int k0 = 0; k0 < K; k0 += kc) {
        int k1 = k0 + kc < K ? k0 + kc : K;
        %(K_REWRITE)s
        int i = r0;
        for (; i + MR <= r1; i += MR) {
            int j = 0;
            for (; j + NR <= N; j += NR) {
                float c[MR][NR];
                for (int a = 0; a < MR; a++)
                    for (int b = 0; b < NR; b++) c[a][b] = C[(long)(i + a) * N + j + b];
                for (int k = k0; k < k1; k++) {
                    const float *restrict bp = B + (long)k * N + j;
                    for (int a = 0; a < MR; a++) {
                        const float av = A[(long)(i + a) * K + k];
                        for (int b = 0; b < NR; b++) c[a][b] += av * bp[b];
                    }
                }
                for (int a = 0; a < MR; a++)
                    for (int b = 0; b < NR; b++) C[(long)(i + a) * N + j + b] = c[a][b];
            }
            %(EDGE)s
        }
        for (; i < r1; i++)
            for (int k = k0; k < k1; k++) {
                const float av = A[(long)i * K + k];
                for (int j = 0; j < N; j++) C[(long)i * N + j] += av * B[(long)k * N + j];
            }
    }
}
"""

EDGE_OK = r"""for (; j < N; j++)
                for (int a = 0; a < MR; a++) {
                    float s = C[(long)(i + a) * N + j];
                    for (int k = k0; k < k1; k++) s += A[(long)(i + a) * K + k] * B[(long)k * N + j];
                    C[(long)(i + a) * N + j] = s;
                }"""

# Reescritas "agressivas" que o sintetizador às vezes propõe. Parecem otimizações;
# o motor NÃO sabe se estão certas. Só o gate decide.
REWRITES = {
    "skip_col_edge": dict(EDGE="/* rewrite: borda de colunas considerada redundante */", K_REWRITE=""),
    "kc_trim_last": dict(EDGE=EDGE_OK, K_REWRITE="if (k1 == K && k1 - k0 > 1) k1--; /* rewrite: último passo fundido */"),
}


def gen_source(g):
    params = dict(T=g["T"], MR=g.get("MR", 1), NR=g.get("NR", 1), KC=g.get("KC", 0),
                  EDGE=EDGE_OK, K_REWRITE="")
    if g.get("rewrite"):
        params.update(REWRITES[g["rewrite"]])
    body = {"naive": NAIVE, "ikj": IKJ, "block": BLOCK}[g["kind"]]
    return COMMON % params + body % params


def genome_key(g):
    return json.dumps(g, sort_keys=True)


def genome_str(g):
    s = "%s T=%d" % (g["kind"], g["T"])
    if g["kind"] == "block":
        s += " MR=%d NR=%d KC=%s" % (g["MR"], g["NR"], g["KC"] or "K")
    if g.get("ISA", "default") != "default":
        s += " " + g["ISA"]
    if g.get("FM"):
        s += " fast-math"
    if g.get("rewrite"):
        s += " +" + g["rewrite"]
    return s


class Synth:
    def __init__(self, cpus, rng, rewrite_rate, isas):
        self.rng = rng
        self.rewrite_rate = rewrite_rate
        self.space = dict(MR=[1, 2, 3, 4, 6, 8], NR=[4, 8, 12, 16, 24, 32, 48, 64],
                          KC=[0, 32, 64, 128, 256, 512], T=sorted({1, max(1, cpus // 2), cpus}),
                          FM=[0, 1], ISA=sorted(isas))

    def random_block(self):
        g = {"kind": "block"}
        for k, vals in self.space.items():
            g[k] = self.rng.choice(vals)
        return g

    def seed_population(self, cpus, size):
        pop = [{"kind": "naive", "T": 1, "FM": 0, "ISA": "default"},
               {"kind": "ikj", "T": 1, "FM": 0, "ISA": "default"},
               {"kind": "block", "T": cpus, "MR": 4, "NR": 16, "KC": 256, "FM": 0, "ISA": "default"},
               dict(self.random_block(), rewrite="skip_col_edge"),
               dict(self.random_block(), rewrite="kc_trim_last")]
        while len(pop) < size:
            pop.append(self.random_block())
        return pop

    def mutate(self, parent):
        g = dict(parent)
        g.pop("rewrite", None)
        if g["kind"] != "block":
            g = self.random_block()
        g.setdefault("ISA", "default")
        for _ in range(self.rng.choice([1, 1, 2])):
            gene = self.rng.choice(list(self.space))
            g[gene] = self.rng.choice(self.space[gene])
        if self.rng.random() < self.rewrite_rate:
            g["rewrite"] = self.rng.choice(sorted(REWRITES))
        return g


def build_candidates(genomes, flags, isas, jobs):
    """Gera C para cada genoma e compila em paralelo. Devolve {key: artefato}."""
    os.makedirs(BUILD, exist_ok=True)

    def one(g):
        src = gen_source(g)
        fl = flags + isas[g.get("ISA", "default")] + (["-ffast-math"] if g.get("FM") else [])
        h = hashlib.sha256((src + " ".join(fl)).encode()).hexdigest()[:16]
        c, so = os.path.join(BUILD, h + ".c"), os.path.join(BUILD, h + ".so")
        if not os.path.exists(so):
            with open(c, "w") as f:
                f.write(src)
            ok, err = compile_so([c], so, fl)
            if not ok:
                return genome_key(g), dict(so=None, error=(err.strip().splitlines() or [""])[-1])
        return genome_key(g), dict(so=so, flags=fl, src_sha256=sha256(c), so_sha256=sha256(so),
                                   fma_instr=count_fma(so))

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        return dict(ex.map(one, genomes))


# --------------------------------------------------------------------------- gate + autotuner

class Gate:
    """Verificação obrigatória: nenhuma implementação é promovida sem passar aqui."""

    def __init__(self, probe, n):
        self.probe = probe
        probe.mlg_fill.argtypes = [F32P, ctypes.c_long, ctypes.c_uint32]
        probe.mlg_poison.argtypes = [F32P, ctypes.c_long]
        probe.mlg_ref.argtypes = [ctypes.c_int] * 3 + [F32P, F32P, F64P, F64P]
        probe.mlg_check.restype = ctypes.c_double
        probe.mlg_check.argtypes = [ctypes.c_int] * 3 + [F32P, F64P, F64P]
        # Formas escolhidas para expor bordas: 1x1x1, primos, não múltiplos de nenhum bloco.
        self.cases = []
        for seed, (m, nn, k) in enumerate([(1, 1, 1), (7, 5, 13), (33, 65, 17), (64, 64, 64),
                                           (97, 40, 130), (n + 3, n - 5, n + 1)]):
            A = (ctypes.c_float * (m * k))()
            B = (ctypes.c_float * (k * nn))()
            C = (ctypes.c_float * (m * nn))()
            R = (ctypes.c_double * (m * nn))()
            S = (ctypes.c_double * (m * nn))()
            probe.mlg_fill(A, m * k, 11 + seed)
            probe.mlg_fill(B, k * nn, 101 + seed)
            probe.mlg_ref(m, nn, k, A, B, R, S)
            self.cases.append((m, nn, k, A, B, C, R, S))

    def shapes(self):
        return ["%dx%dx%d" % c[:3] for c in self.cases]

    def judge(self, fn):
        ratios = []
        for m, nn, k, A, B, C, R, S in self.cases:
            self.probe.mlg_poison(C, m * nn)
            fn(m, nn, k, A, B, C)
            ratios.append(self.probe.mlg_check(m, nn, k, C, R, S))
            if ratios[-1] > 1:
                return False, ratios
        return True, ratios


def load_kernel(so):
    fn = ctypes.CDLL(so).mlg_kernel
    fn.argtypes = [ctypes.c_int] * 3 + [F32P, F32P, F32P]
    fn.restype = None
    return fn


def timed(fn, n, A, B, C):
    t0 = time.perf_counter()
    fn(n, n, n, A, B, C)
    return time.perf_counter() - t0


def bench(fn, n, A, B, C, budget):
    """Amostras cruas de tempo (s) de chamadas completas, incluindo criação de threads.
    1 aquecimento descartado; >= 5 amostras; dados quentes no cache (A, B, C = 3n² floats)."""
    fn(n, n, n, A, B, C)
    samples, total = [], 0.0
    while (total < budget or len(samples) < 5) and len(samples) < 200:
        samples.append(timed(fn, n, A, B, C))
        total += samples[-1]
    return samples


def stats(samples, n):
    flop = 2.0 * n ** 3  # algoritmo clássico: n³ multiplicações + n³ somas
    med = statistics.median(samples)
    sd = statistics.pstdev(samples)
    return dict(gflops_median=flop / med / 1e9, gflops_best=flop / min(samples) / 1e9,
                gflops_worst=flop / max(samples) / 1e9, cv=sd / statistics.mean(samples), n_samples=len(samples))


def duel(fa, fb, n, A, B, C, rounds=9, per_round=5):
    """Execuções intercaladas A/B para que ruído de máquina (turbo, vizinhos de VM) afete os dois."""
    ta, tb = [], []
    for _ in range(rounds):
        ta.append(statistics.median([timed(fa, n, A, B, C) for _ in range(per_round)]))
        tb.append(statistics.median([timed(fb, n, A, B, C) for _ in range(per_round)]))
    return ta, tb


# --------------------------------------------------------------------------- relatório

def human(x, unit):
    for p, s in ((1e12, "T"), (1e9, "G"), (1e6, "M"), (1e3, "k")):
        if abs(x) >= p:
            return "%.2f %s%s" % (x / p, s, unit)
    return "%.2f %s" % (x, unit)


def report_limits(dna, champ, n, args):
    peak, bw = dna["peak_all"], dna["bw_all"]
    best = champ["stats"]["gflops_median"] * 1e9
    ridge = peak / bw
    ai = n / 6.0  # 2n³ FLOP / (3 matrizes x n² x 4 B), tráfego compulsório
    out = {}
    print("\n" + "=" * 78)
    print("  O LIMITE, MEDIDO (não desenhado)")
    print("=" * 78)
    print("  Todo teto aqui é EMPÍRICO: a melhor medição de um probe. O teto verdadeiro do chip")
    print("  é >= ele; logo toda '% do teto' abaixo é um LIMITE SUPERIOR da eficiência.\n")
    print("  P_compute (empírico)     : %s  (%d threads; 1 thread %s; via ACC=%d %s)"
          % (human(peak, "FLOP/s"), args.cpus, human(dna["peak_1t"], "FLOP/s"),
             dna["peak_from"]["acc"], dna["peak_from"]["isa"]))
    print("  frequência efetiva       : %.2f GHz (1 thread) / %.2f GHz (todas)"
          % (dna["freq_1t_hz"] / 1e9, dna["freq_all_hz"] / 1e9))
    print("  P_memory DRAM (empírico) : %s  (1 thread %s; conjunto %s = %.1fx o L%d)"
          % (human(bw, "B/s"), human(dna["bw_1t"], "B/s"), human(dna["bw_working_set"], "B"),
             dna["bw_dram_ws_over_llc"] or 0, dna["llc_level"]))
    if dna.get("bw_llc_all"):
        print("  banda dentro do L%d       : %s  (não é DRAM; só referência)" % (dna["llc_level"], human(dna["bw_llc_all"], "B/s")))
    print("  ridge                    : %.1f FLOP/byte" % ridge)
    reach = min(peak, ai * bw)
    wall = "CÁLCULO" if ai * bw >= peak else "MEMÓRIA"
    print("  P_reachable = min(P_compute, BW x I) com I = %.0f FLOP/B  ->  parede de %s" % (ai, wall))
    pct = 100.0 * best / reach
    print("  Campeão (mediana)        : %s = <= %.1f%% de P_reachable" % (human(best, "FLOP/s"), pct))
    print("  -> Folga restante com ESTE algoritmo (2n³ FLOP) e ESTE contrato: >= %.2fx." % (reach / best))
    out.update(p_reachable=reach, wall=wall, intensity=ai, ridge=ridge, efficiency_upper_bound=best / reach)

    P = args.params
    print("\n  Decodificação de um modelo de %s parâmetros, batch 1:" % human(P, "").strip())
    print("  cada token lê todos os pesos => intensidade ~2/b FLOP/B, bem abaixo do ridge")
    print("    %-6s %10s %18s %16s" % ("formato", "pesos", "teto memória DRAM", "teto cálculo"))
    dec = {}
    for name, b in (("FP32", 4), ("FP16", 2), ("INT8", 1), ("INT4", 0.5)):
        dec[name] = dict(mem_tok_s=bw / (P * b), compute_tok_s=peak / (2 * P))
        print("    %-6s %10s %15.1f t/s %13.1f t/s" % (name, human(P * b, "B"), bw / (P * b), peak / (2 * P)))
    out["decode"] = dec
    if args.link_mbps:
        link = args.link_mbps * 1e6 / 8
        print("\n  P_network: pesos remotos via link de %.0f Mbit/s" % args.link_mbps)
        for name, b in (("FP16", 2), ("INT8", 1), ("INT4", 0.5)):
            print("    %-6s 1 token a cada %.1f s" % (name, P * b / link))
        print("  -> Pesos ficam junto do cálculo; o que atravessa fronteiras são ativações.")
        out["network_s_per_token_int8"] = P / link
    if args.link_ms:
        print("  Divisão em %d aparelhos: <= %.0f tokens/s só de latência (RTT %.1f ms)"
              % (args.splits + 1, 1000.0 / (args.link_ms * args.splits), args.link_ms))
    if args.watts:
        measured = dna.get("temp_c_after") is not None
        temp_k = 273.15 + (dna["temp_c_after"] if measured else 27.0)
        ktln2 = 1.380649e-23 * temp_k * math.log(2)
        print("\n  Referência de Landauer (T = %.1f K, %s): kT·ln2 = %.2e J por bit APAGADO irreversivelmente"
              % (temp_k, "medida" if measured else "presumida, sem sensor", ktln2))
        print("  potência medida %.2f W  =>  equivalente a %.2e apagamentos de bit/s"
              % (args.watts, args.watts / ktln2))
        print("  AVISO: isto NÃO é um limite de J/FLOP. Um FLOP não apaga um número fixo de bits,")
        print("  e computação logicamente reversível não está sujeita a esse custo por operação.")
        out["landauer"] = dict(temp_k=temp_k, temp_measured=measured, ktln2_j=ktln2, watts=args.watts,
                               equivalent_erasures_per_s=args.watts / ktln2, note="not a FLOP efficiency bound")
    print("=" * 78)
    return out


# --------------------------------------------------------------------------- laço principal

def main():
    ap = argparse.ArgumentParser(description="MLG: síntese + verificação + autotuning em hardware real")
    ap.add_argument("--n", type=int, default=512, help="tamanho da matmul de benchmark")
    ap.add_argument("--gens", type=int, default=4, help="gerações de busca")
    ap.add_argument("--pop", type=int, default=12, help="candidatos por geração")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--budget", type=float, default=0.3, help="segundos de benchmark por candidato")
    ap.add_argument("--rewrite-rate", type=float, default=0.15, help="chance de reescrita não provada")
    ap.add_argument("--bw-mb", type=float, default=0, help="força o conjunto de trabalho do teste de banda (MB)")
    ap.add_argument("--params", type=float, default=101e6, help="parâmetros do modelo para o teto de decodificação")
    ap.add_argument("--link-mbps", type=float, default=0, help="banda do link entre aparelhos (ex.: 20)")
    ap.add_argument("--link-ms", type=float, default=0, help="RTT do link entre aparelhos (ex.: 5)")
    ap.add_argument("--splits", type=int, default=1, help="fronteiras de divisão do modelo entre aparelhos")
    ap.add_argument("--watts", type=float, default=0, help="consumo medido durante a carga (ex.: 1.5)")
    ap.add_argument("--quick", action="store_true", help="busca curta")
    args = ap.parse_args()
    if args.quick:
        args.gens, args.pop, args.budget = 2, 8, 0.15
    args.cpus = os.cpu_count() or 1
    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    rng = random.Random(seed)

    flags = detect_flags()
    isas = isa_variants(flags)
    fingerprint = "%s | %s | %d cpus | %s MB" % (platform.machine(), cpu_model(), args.cpus, meminfo_mb("MemTotal"))
    print("MLG :: território = %s" % fingerprint)
    print("     compilador = %s" % compiler_version())
    print("     flags = %s   variantes de ISA = %s" % (" ".join(flags), ", ".join(sorted(isas))))

    ok, err = compile_so([os.path.join(HERE, "probe.c")], os.path.join(BUILD, "probe.so"), flags)
    if not ok:
        sys.exit(err)
    probe = ctypes.CDLL(os.path.join(BUILD, "probe.so"))
    gate_hash = sha256(os.path.join(HERE, "probe.c"))
    print("     raiz de confiança (probe.c, gate) sha256 = %s" % gate_hash[:16])

    print("\n[1] DNA do hardware (medido agora, neste estado físico)")
    dna = probe_hardware(probe, flags, isas, args.cpus, args)
    for p in dna["peak_variants"]:
        print("    pico ACC=%-3d %-7s 1t %8.1f  todas %8.1f GFLOP/s" % (
            p["acc"], p["isa"], max(p["samples_1t"]) / 1e9, max(p["samples_all"]) / 1e9))
    print("    banda DRAM 1t %s, todas %s (conjunto %s)" % (
        human(dna["bw_1t"], "B/s"), human(dna["bw_all"], "B/s"), human(dna["bw_working_set"], "B")))
    print("    frequência efetiva %.2f / %.2f GHz" % (dna["freq_1t_hz"] / 1e9, dna["freq_all_hz"] / 1e9))
    if dna["temp_c_before"] is not None:
        print("    temperatura %.1f -> %.1f °C" % (dna["temp_c_before"], dna["temp_c_after"]))

    state = {}
    if os.path.exists(STATE):
        with open(STATE) as f:
            state = json.load(f)
    incumbent = state.get("champions", {}).get(fingerprint, {}).get(str(args.n))

    n = args.n
    gate = Gate(probe, n)
    A = (ctypes.c_float * (n * n))()
    B = (ctypes.c_float * (n * n))()
    C = (ctypes.c_float * (n * n))()
    probe.mlg_fill(A, n * n, 1)
    probe.mlg_fill(B, n * n, 2)

    synth = Synth(args.cpus, rng, args.rewrite_rate, isas)
    population = synth.seed_population(args.cpus, args.pop)
    if incumbent:
        population.insert(0, incumbent["genome"])
        print("\n    campeão anterior encontrado (%.2f GFLOP/s) — vai ter que se provar de novo"
              % incumbent["gflops"])

    results = {}
    for gen in range(args.gens):
        population = [g for g in {genome_key(g): g for g in population}.values()
                      if genome_key(g) not in results]
        print("\n[2.%d] geração %d: sintetizando %d candidatos" % (gen, gen, len(population)))
        built = build_candidates(population, flags, isas, args.cpus)
        for g in population:
            key = genome_key(g)
            art = built[key]
            rec = dict(genome=g, generation=gen, ok=False, level=0, artifact=art)
            results[key] = rec
            if not art["so"]:
                rec["why"] = "não compilou: " + art["error"]
                continue
            fn = load_kernel(art["so"])
            env = fp_env(probe)
            rec["contract"] = dict(
                dtype="float32", rounding=env["rounding"], ftz=env["ftz"], daz=env["daz"],
                fma=None if art["fma_instr"] is None else art["fma_instr"] > 0,
                reassociation="allowed (-ffast-math)" if g.get("FM") else "compiler-default (no -ffast-math)",
                overflow="impossible: |x|<=1, |sum|<=K", underflow="impossible: values are multiples of 2^-69",
                bound="gamma_K = K*u/(1-K*u), u=2^-24 (Higham §3.1), any evaluation order",
                compiler=compiler_version(), flags=art["flags"], isa=platform.machine() + "/" + g.get("ISA", "default"))
            if env["rounding"] != "nearest":
                rec["why"] = "contrato violado: arredondamento %s" % env["rounding"]
                print("    REJEITADO  %-50s %s" % (genome_str(g), rec["why"]))
                continue
            ok, ratios = gate.judge(fn)
            rec["gate_ratios"] = dict(zip(gate.shapes(), ratios))
            if not ok:
                rec["why"] = "contraexemplo: erro %.3gx o limite provado em %s" % (ratios[-1], gate.shapes()[len(ratios) - 1])
                print("    REJEITADO  %-50s %s" % (genome_str(g), rec["why"]))
                continue
            rec["ok"], rec["level"] = True, 2
            rec["samples_s"] = bench(fn, n, A, B, C, args.budget)
            rec["stats"] = stats(rec["samples_s"], n)
            st = rec["stats"]
            print("    ok %7.2f GFLOP/s (cv %4.1f%%)  %-46s erro/limite=%.2f" % (
                st["gflops_median"], 100 * st["cv"], genome_str(g), max(ratios)))
        ranked = sorted((r for r in results.values() if r["ok"]), key=lambda r: -r["stats"]["gflops_median"])
        parents = [r["genome"] for r in ranked[:4]] or [synth.random_block()]
        population = [synth.mutate(rng.choice(parents)) for _ in range(args.pop)]

    ranked = sorted((r for r in results.values() if r["ok"] and r["level"] >= PROMOTION_MIN_LEVEL),
                    key=lambda r: -r["stats"]["gflops_median"])
    winner = ranked[0]
    baseline = next((r for r in ranked if r["genome"]["kind"] == "naive"), None)
    rejected = [r for r in results.values() if not r["ok"]]

    print("\n[3] seleção (por mediana)")
    print("    candidatos avaliados: %d, reprovados: %d" % (len(results), len(rejected)))
    print("    vencedor: %s -> %.2f GFLOP/s mediana [%.2f .. %.2f]" % (
        genome_str(winner["genome"]), winner["stats"]["gflops_median"],
        winner["stats"]["gflops_worst"], winner["stats"]["gflops_best"]))
    if baseline:
        print("    versão ingênua: %.2f GFLOP/s  (vencedor = %.1fx)" % (
            baseline["stats"]["gflops_median"], winner["stats"]["gflops_median"] / baseline["stats"]["gflops_median"]))

    champ, duel_rec = winner, None
    if incumbent and genome_key(incumbent["genome"]) != genome_key(winner["genome"]):
        inc = results.get(genome_key(incumbent["genome"]))
        if inc and inc["ok"]:
            ti, tw = duel(load_kernel(inc["artifact"]["so"]), load_kernel(winner["artifact"]["so"]), n, A, B, C)
            gain = statistics.median(ti) / statistics.median(tw)
            noise = max(stats(ti, n)["cv"], stats(tw, n)["cv"])
            need = 1 + max(0.02, 2 * noise)
            duel_rec = dict(incumbent_s=ti, challenger_s=tw, gain=gain, required=need)
            print("    duelo intercalado: desafiante %.3fx o campeão (exigido > %.3fx)" % (gain, need))
            if gain <= need:
                champ = inc
                print("    desafiante não venceu além do ruído — campeão mantido (rollback)")
        else:
            print("    campeão anterior REPROVOU agora — destituído")
    state.setdefault("champions", {}).setdefault(fingerprint, {})[str(n)] = dict(
        genome=champ["genome"], gflops=champ["stats"]["gflops_median"], so_sha256=champ["artifact"]["so_sha256"],
        level=champ["level"], when=time.strftime("%Y-%m-%d %H:%M:%S"))
    with open(STATE, "w") as f:
        json.dump(state, f, indent=2, sort_keys=True)
    print("    campeão: %s [nível %d %s]" % (genome_str(champ["genome"]), champ["level"], LEVELS[champ["level"]]))

    limits = report_limits(dna, champ, n, args)

    os.makedirs(REPORTS, exist_ok=True)
    path = os.path.join(REPORTS, "%s-%s.json" % (platform.node() or "host", time.strftime("%Y%m%d-%H%M%S")))
    raw = dict(
        schema="mlg-report/1", when=time.strftime("%Y-%m-%dT%H:%M:%S%z"), argv=sys.argv[1:], seed=seed,
        fingerprint=fingerprint, python=sys.version.split()[0], compiler=compiler_version(), flags=flags,
        isa_variants=isas, trust_root={"probe.c_sha256": gate_hash, "promotion_min_level": PROMOTION_MIN_LEVEL},
        methodology=dict(
            flop_count="2*n^3 (classical algorithm)", timer="time.perf_counter, wall clock, per full call",
            includes="thread creation/join per call; ctypes call overhead",
            warmup="1 discarded call per candidate", cache="hot: A,B,C reused (3n^2 floats)",
            ranking="median of samples", promotion="interleaved duel, gain > max(2%, 2*cv)",
            peak="max over probe variants (ACC x ISA), FMA chain with ACC independent accumulators",
            bandwidth="STREAM triad 24 B/iter, working set >= 4x LLC unless --bw-mb"),
        dna=dna, gate_shapes=gate.shapes(), candidates=list(results.values()), champion=genome_key(champ["genome"]),
        duel=duel_rec, limits=limits)
    with open(path, "w") as f:
        json.dump(raw, f, indent=1, sort_keys=True)
    print("\n  relatório bruto: %s" % os.path.relpath(path, os.getcwd()))


if __name__ == "__main__":
    main()

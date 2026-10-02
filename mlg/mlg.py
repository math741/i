#!/usr/bin/env python3
"""MLG semente: o laço central rodando em silício de verdade.

    hardware real -> DNA medido -> síntese de candidatos -> gate com prova
    -> benchmark -> seleção -> campeão persistido (com rollback) -> repete

E no fim mostra o LIMITE: o quanto falta até o teto físico medido da máquina.
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
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, ".build")
STATE = os.path.join(HERE, "mlg_state.json")
CC = os.environ.get("CC", "cc")

F32P = ctypes.POINTER(ctypes.c_float)
F64P = ctypes.POINTER(ctypes.c_double)


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


def temperature():
    temps = []
    for p in glob.glob("/sys/class/thermal/thermal_zone*/temp"):
        v = read_first(p)
        if v and v.strip().lstrip("-").isdigit():
            temps.append(int(v) / 1000.0)
    return max(temps) if temps else None


def mem_total_mb():
    for line in (read_first("/proc/meminfo", "") or "").splitlines():
        if line.startswith("MemTotal:"):
            return int(line.split()[1]) // 1024
    return None


def probe_hardware(probe, peak, cpus, bw_mb):
    probe.mlg_bandwidth.restype = ctypes.c_double
    probe.mlg_bandwidth.argtypes = [ctypes.c_long, ctypes.c_int, ctypes.c_int]
    peak.mlg_peak_flops.restype = ctypes.c_double
    peak.mlg_peak_flops.argtypes = [ctypes.c_long, ctypes.c_int]

    def calibrated_peak(threads):
        iters = 20000
        while True:
            t0 = time.perf_counter()
            peak.mlg_peak_flops(iters, threads)
            if time.perf_counter() - t0 > 0.15:
                break
            iters *= 4
        return max(peak.mlg_peak_flops(iters, threads) for _ in range(3))

    n = int(bw_mb * 1024 * 1024 / 8 / 3)
    return {
        "peak_1t": calibrated_peak(1),
        "peak_all": calibrated_peak(cpus),
        "bw_1t": probe.mlg_bandwidth(n, 1, 4),
        "bw_all": probe.mlg_bandwidth(n, cpus, 4),
        "temp_c": temperature(),
    }


# --------------------------------------------------------------------------- motor de síntese

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
    if g.get("FM"):
        s += " fast-math"
    if g.get("rewrite"):
        s += " +" + g["rewrite"]
    return s


class Synth:
    def __init__(self, cpus, rng, rewrite_rate):
        self.rng = rng
        self.rewrite_rate = rewrite_rate
        self.space = dict(MR=[1, 2, 3, 4, 6, 8], NR=[4, 8, 12, 16, 24, 32, 48, 64],
                          KC=[0, 32, 64, 128, 256, 512], T=sorted({1, max(1, cpus // 2), cpus}),
                          FM=[0, 1])

    def random_block(self):
        g = {"kind": "block"}
        for k, vals in self.space.items():
            g[k] = self.rng.choice(vals)
        return g

    def seed_population(self, cpus, size):
        pop = [{"kind": "naive", "T": 1, "FM": 0},
               {"kind": "ikj", "T": 1, "FM": 0},
               {"kind": "block", "T": cpus, "MR": 4, "NR": 16, "KC": 256, "FM": 0},
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
        for _ in range(self.rng.choice([1, 1, 2])):
            gene = self.rng.choice(list(self.space))
            g[gene] = self.rng.choice(self.space[gene])
        if self.rng.random() < self.rewrite_rate:
            g["rewrite"] = self.rng.choice(sorted(REWRITES))
        return g


def build_candidates(genomes, flags, jobs):
    """Gera C para cada genoma e compila tudo em paralelo. Devolve {key: (so|None, erro)}."""
    os.makedirs(BUILD, exist_ok=True)

    def one(g):
        src = gen_source(g)
        fl = flags + (["-ffast-math"] if g.get("FM") else [])
        h = hashlib.sha1((src + " ".join(fl)).encode()).hexdigest()[:16]
        c, so = os.path.join(BUILD, h + ".c"), os.path.join(BUILD, h + ".so")
        if not os.path.exists(so):
            with open(c, "w") as f:
                f.write(src)
            ok, err = compile_so([c], so, fl)
            if not ok:
                return genome_key(g), (None, err.strip().splitlines()[-1:] if err else "")
        return genome_key(g), (so, "")

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

    def judge(self, fn):
        worst = 0.0
        for m, nn, k, A, B, C, R, S in self.cases:
            self.probe.mlg_poison(C, m * nn)
            fn(m, nn, k, A, B, C)
            worst = max(worst, self.probe.mlg_check(m, nn, k, C, R, S))
            if worst > 1:
                return False, worst, "%dx%dx%d" % (m, nn, k)
        return True, worst, ""


def load_kernel(so):
    lib = ctypes.CDLL(so)
    fn = lib.mlg_kernel
    fn.argtypes = [ctypes.c_int] * 3 + [F32P, F32P, F32P]
    fn.restype = None
    return fn


def bench(fn, n, A, B, C, budget):
    fn(n, n, n, A, B, C)
    best, total, reps = float("inf"), 0.0, 0
    while (total < budget or reps < 2) and reps < 50:
        t0 = time.perf_counter()
        fn(n, n, n, A, B, C)
        dt = time.perf_counter() - t0
        best, total, reps = min(best, dt), total + dt, reps + 1
    return 2.0 * n ** 3 / best / 1e9


# --------------------------------------------------------------------------- relatório

def human(x, unit):
    for p, s in ((1e12, "T"), (1e9, "G"), (1e6, "M"), (1e3, "k")):
        if abs(x) >= p:
            return "%.2f %s%s" % (x / p, s, unit)
    return "%.2f %s" % (x, unit)


def report_limits(dna, champ, n, args):
    peak, bw = dna["peak_all"], dna["bw_all"]
    best = champ["gflops"] * 1e9
    ridge = peak / bw
    ai = n / 6.0
    print("\n" + "=" * 74)
    print("  O LIMITE, MEDIDO (não desenhado)")
    print("=" * 74)
    print("  Teto de cálculo medido   : %s  (%d threads; 1 thread: %s)"
          % (human(peak, "FLOP/s"), args.cpus, human(dna["peak_1t"], "FLOP/s")))
    print("  Teto de memória medido   : %s  (1 thread: %s)" % (human(bw, "B/s"), human(dna["bw_1t"], "B/s")))
    print("  Ponto de virada (ridge)  : %.1f FLOP/byte" % ridge)
    print("  %-25s: intensidade %.0f FLOP/byte -> %s" % (
        "matmul %dx%d" % (n, n), ai, "limitada por CÁLCULO" if ai > ridge else "limitada por MEMÓRIA"))
    ceiling = min(peak, ai * bw)
    pct = 100.0 * best / ceiling
    print("  Campeão                  : %s = %.1f%% do teto medido" % (human(best, "FLOP/s"), pct))
    if pct < 100:
        print("  -> Tudo que qualquer compilador, IA ou humano ainda pode ganhar com ESTE algoritmo")
        print("     (2n³ FLOPs) nesta máquina: %.2fx. Passar disso exige mudar a matemática" % (ceiling / best))
        print("     (fazer menos operações) ou mudar o silício.")
    else:
        print("  -> Passou do microbenchmark: o teto real do chip está acima do que o probe mediu.")

    P = args.params
    print("\n  Modelo de %s parâmetros, decodificando 1 token por vez (batch 1):" % human(P, "").strip())
    print("  cada token lê TODOS os pesos uma vez => tokens/s <= banda / bytes_dos_pesos")
    print("    %-6s %10s %16s %16s" % ("formato", "pesos", "teto memória", "teto cálculo"))
    for name, b in (("FP32", 4), ("FP16", 2), ("INT8", 1), ("INT4", 0.5)):
        print("    %-6s %10s %13.1f t/s %13.1f t/s" % (name, human(P * b, "B"), bw / (P * b), peak / (2 * P)))
    if args.link_mbps:
        link = args.link_mbps * 1e6 / 8
        print("\n  Pesos fora da RAM local, via link de %.0f Mbit/s (\"ONE MEMORY\" na rede):" % args.link_mbps)
        for name, b in (("FP16", 2), ("INT8", 1), ("INT4", 0.5)):
            print("    %-6s 1 token a cada %.1f s" % (name, P * b / link))
        print("  -> Streaming de pesos pela rede não escala para decodificação. Pesos ficam parados;")
        print("     o que viaja são ativações (KB por token), e cada fronteira custa >= 1 RTT.")
    if args.link_ms:
        print("  Dividir o modelo em %d aparelhos: teto de latência <= %.0f tokens/s (RTT %.1f ms)"
              % (args.splits + 1, 1000.0 / (args.link_ms * args.splits), args.link_ms))
    if args.watts:
        j_flop = args.watts / best
        landauer = 1.380649e-23 * 300 * math.log(2)
        print("\n  Energia: %.2e J/FLOP a %.1f W. Piso de Landauer (300 K, 1 bit apagado): %.2e J."
              % (j_flop, args.watts, landauer))
        print("  -> Distância até o limite termodinâmico: ~10^%.0f. Esse é o teto que nenhuma"
              % math.log10(j_flop / landauer))
        print("     arquitetura atravessa; todo o resto do caminho é engenharia.")
    print("=" * 74)


# --------------------------------------------------------------------------- laço principal

def main():
    ap = argparse.ArgumentParser(description="MLG: síntese + verificação + autotuning em hardware real")
    ap.add_argument("--n", type=int, default=512, help="tamanho da matmul de benchmark")
    ap.add_argument("--gens", type=int, default=4, help="gerações de busca")
    ap.add_argument("--pop", type=int, default=12, help="candidatos por geração")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--budget", type=float, default=0.3, help="segundos de benchmark por candidato")
    ap.add_argument("--rewrite-rate", type=float, default=0.15, help="chance de reescrita não provada")
    ap.add_argument("--bw-mb", type=float, default=96, help="memória usada no teste de banda")
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
    rng = random.Random(args.seed)

    flags = detect_flags()
    fingerprint = "%s | %s | %d cpus | %s MB" % (platform.machine(), cpu_model(), args.cpus, mem_total_mb())
    print("MLG :: território = %s" % fingerprint)
    print("     flags descobertas = %s" % " ".join(flags))

    ok1, e1 = compile_so([os.path.join(HERE, "probe.c")], os.path.join(BUILD, "probe.so"), flags)
    ok2, e2 = compile_so([os.path.join(HERE, "peak.c")], os.path.join(BUILD, "peak.so"), flags + ["-ffast-math"])
    if not (ok1 and ok2):
        sys.exit(e1 or e2)
    probe = ctypes.CDLL(os.path.join(BUILD, "probe.so"))
    peak = ctypes.CDLL(os.path.join(BUILD, "peak.so"))

    print("\n[1] DNA do hardware (medido agora, neste estado físico)")
    dna = probe_hardware(probe, peak, args.cpus, args.bw_mb)
    print("    FLOP/s  1 thread %-14s todas %s" % (human(dna["peak_1t"], ""), human(dna["peak_all"], "")))
    print("    banda   1 thread %-14s todas %s" % (human(dna["bw_1t"], "B/s"), human(dna["bw_all"], "B/s")))
    if dna["temp_c"] is not None:
        print("    temperatura %.1f °C" % dna["temp_c"])

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

    synth = Synth(args.cpus, rng, args.rewrite_rate)
    population = synth.seed_population(args.cpus, args.pop)
    if incumbent:
        population.insert(0, incumbent["genome"])
        print("\n    campeão anterior encontrado (%.2f GFLOP/s) — vai ter que se provar de novo"
              % incumbent["gflops"])

    results = {}
    rejected = 0
    for gen in range(args.gens):
        population = [g for g in {genome_key(g): g for g in population}.values()
                      if genome_key(g) not in results]
        print("\n[2.%d] geração %d: sintetizando %d candidatos" % (gen, gen, len(population)))
        built = build_candidates(population, flags, args.cpus)
        for g in population:
            key = genome_key(g)
            so, err = built[key]
            if not so:
                results[key] = dict(genome=g, ok=False, gflops=0, why="não compilou")
                continue
            fn = load_kernel(so)
            ok, ratio, where = gate.judge(fn)
            if not ok:
                rejected += 1
                results[key] = dict(genome=g, ok=False, gflops=0,
                                    why="erro %.3gx acima do limite provado em %s" % (ratio, where))
                print("    REJEITADO  %-46s %s" % (genome_str(g), results[key]["why"]))
                continue
            gf = bench(fn, n, A, B, C, args.budget)
            results[key] = dict(genome=g, ok=True, gflops=gf, ratio=ratio)
            print("    ok %7.2f GFLOP/s  %-44s erro/limite=%.2f" % (gf, genome_str(g), ratio))
        ranked = sorted((r for r in results.values() if r["ok"]), key=lambda r: -r["gflops"])
        parents = [r["genome"] for r in ranked[:4]] or [synth.random_block()]
        population = [synth.mutate(rng.choice(parents)) for _ in range(args.pop)]

    ranked = sorted((r for r in results.values() if r["ok"]), key=lambda r: -r["gflops"])
    winner = ranked[0]
    baseline = next((r for r in ranked if r["genome"]["kind"] == "naive"), None)

    print("\n[3] seleção")
    print("    candidatos avaliados: %d, reprovados pelo gate: %d" % (len(results), rejected))
    print("    vencedor: %s -> %.2f GFLOP/s" % (genome_str(winner["genome"]), winner["gflops"]))
    if baseline:
        print("    versão ingênua: %.2f GFLOP/s  (vencedor = %.1fx)"
              % (baseline["gflops"], winner["gflops"] / baseline["gflops"]))

    champ = winner
    if incumbent and genome_key(incumbent["genome"]) != genome_key(winner["genome"]):
        inc = results.get(genome_key(incumbent["genome"]))
        if inc and inc["ok"] and winner["gflops"] < inc["gflops"] * 1.02:
            champ = inc
            print("    vencedor não superou o campeão por >2%% — campeão mantido (rollback)")
    state.setdefault("champions", {}).setdefault(fingerprint, {})[str(n)] = dict(
        genome=champ["genome"], gflops=champ["gflops"], flags=flags,
        dna=dna, when=time.strftime("%Y-%m-%d %H:%M:%S"))
    with open(STATE, "w") as f:
        json.dump(state, f, indent=2, sort_keys=True)
    print("    campeão persistido em %s" % os.path.relpath(STATE))

    report_limits(dna, champ, n, args)


if __name__ == "__main__":
    main()

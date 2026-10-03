"""Núcleo da MLG. Não conhece nenhuma operação: recebe SPEC + TRANSFORMATION SPACE +
EVIDENCE OBLIGATIONS + COST MODEL (um objeto de ops/) e devolve um campeão verificado
e um relatório de fronteira dizendo qual parede impede o próximo avanço."""
import concurrent.futures
import ctypes
import hashlib
import json
import os
import platform
import statistics
import time

from hw import BUILD, compile_so, compiler_version, count_fma, fp_env, sha256

F32P = ctypes.POINTER(ctypes.c_float)
F64P = ctypes.POINTER(ctypes.c_double)

# Escada de evidência. Promoção exige o nível mínimo; E0 nunca substitui campeão de classe superior.
LEVELS = {5: "E5 proof checked", 4: "E4 certified transformation", 3: "E3 bounded numerical guarantee",
          2: "E2 differential + bound", 1: "E1 empirical", 0: "E0 experimental"}
PROMOTION_MIN_LEVEL = 2

# Pré-âmbulo comum: ABI única (dims, entradas, saída), threads por blocos de linhas, scratch.
PRELUDE = r"""
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#define THREADS %(T)d
#define RALIGN %(RALIGN)d
static float *g_scratch;
static long g_cap;
"""

DRIVER = r"""
typedef struct { const int *dims; const float *const *in; float *out; int r0, r1; } job_t;
static void *run(void *p) {
    job_t *j = (job_t *)p;
    rows(j->dims, j->in, j->out, j->r0, j->r1);
    return 0;
}
void mlg_kernel(const int *dims, const float *const *in, float *out) {
    long need = %(SCRATCH)s;
    if (need > g_cap) { free(g_scratch); g_scratch = malloc(need * sizeof(float)); g_cap = need; }
    const int R = dims[0];
    int T = THREADS < R ? THREADS : R;
    if (T <= 1) { rows(dims, in, out, 0, R); return; }
    pthread_t th[64]; job_t jb[64]; int b[65];
    for (int t = 0; t < T; t++) b[t] = (int)((long)R * t / T / RALIGN * RALIGN);
    b[T] = R;
    for (int t = 0; t < T; t++) {
        jb[t] = (job_t){dims, in, out, b[t], b[t + 1]};
        if (t) pthread_create(&th[t], 0, run, &jb[t]);
    }
    run(&jb[0]);
    for (int t = 1; t < T; t++) pthread_join(th[t], 0);
}
"""


def gen_source(op, g):
    body, extra = op.source(g)
    params = dict(T=g["T"], **extra)
    return PRELUDE % params + body + DRIVER % params


def genome_key(g):
    return json.dumps(g, sort_keys=True)


def genome_str(op, g):
    s = "%s T=%d" % (op.describe(g), g["T"])
    if g.get("ISA", "default") != "default":
        s += " " + g["ISA"]
    if g.get("FM"):
        s += " fast-math"
    if g.get("rewrite"):
        s += " +" + g["rewrite"]
    return s


# --------------------------------------------------------------------------- síntese

class Synth:
    def __init__(self, op, cpus, rng, rewrite_rate, isas):
        self.op, self.rng, self.rewrite_rate = op, rng, rewrite_rate
        self.common = dict(T=sorted({1, max(1, cpus // 2), cpus}), FM=[0, 1], ISA=sorted(isas))

    def space(self, kind):
        return dict(self.op.kinds[kind], **self.common)

    def random(self, kind=None):
        kinds = [k for k, w in self.op.kind_weights.items() for _ in range(w)]
        kind = kind or self.rng.choice(kinds)
        g = {"kind": kind}
        for gene, vals in self.space(kind).items():
            g[gene] = self.rng.choice(vals)
        return g

    def sanitize(self, g):
        """Adapta um genoma de outro contexto (histórico, outra máquina) a este espaço."""
        if g.get("kind") not in self.op.kinds:
            return None
        out = {"kind": g["kind"]}
        for gene, vals in self.space(g["kind"]).items():
            v = g.get(gene)
            out[gene] = v if v in vals else (min(vals, key=lambda x: abs(x - v)) if isinstance(v, int)
                                             and all(isinstance(x, int) for x in vals) else vals[0])
        return out

    def seed_population(self, cpus, size, prior):
        pop = [self.sanitize(g) for g in self.op.seeds(cpus)] + [g for g in prior]
        for kind, rws in self.op.rewrites.items():
            for name in sorted(rws):
                pop.append(dict(self.random(kind), rewrite=name))
        while len(pop) < size:
            pop.append(self.random())
        return pop

    def mutate(self, parent):
        if not self.op.kinds[parent["kind"]] and self.rng.random() < 0.5:
            g = self.random()  # tipos sem genes próprios: explorar outro tipo
        else:
            g = {k: v for k, v in parent.items() if k != "rewrite"}
            space = self.space(g["kind"])
            for _ in range(self.rng.choice([1, 1, 2])):
                gene = self.rng.choice(sorted(space))
                g[gene] = self.rng.choice(space[gene])
        rws = self.op.rewrites.get(g["kind"], {})
        if rws and self.rng.random() < self.rewrite_rate:
            g["rewrite"] = self.rng.choice(sorted(rws))
        return g


def build_candidates(op, genomes, flags, isas, jobs):
    os.makedirs(BUILD, exist_ok=True)

    def one(g):
        src = gen_source(op, g)
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


# --------------------------------------------------------------------------- dados, gate, medição

class Case:
    """Entradas + saída de uma forma; com referência e tolerância se for caso do gate."""

    def __init__(self, op, probe, dims, scales, seed, with_ref, label=None):
        self.dims, self.label = dims, label or "x".join(map(str, dims))
        sizes, nout = op.input_sizes(dims)
        self.ins = []
        for k, (n, sc) in enumerate(zip(sizes, scales)):
            a = (ctypes.c_float * n)()
            probe.mlg_fill_scaled(a, n, 1000 * seed + k + 1, sc)
            self.ins.append(a)
        self.nout = nout
        self.out = (ctypes.c_float * nout)()
        self.c_dims = (ctypes.c_int * len(dims))(*dims)
        self.c_ins = (F32P * len(self.ins))(*[ctypes.cast(a, F32P) for a in self.ins])
        if with_ref:
            self.R = (ctypes.c_double * nout)()
            self.T = (ctypes.c_double * nout)()
            op.reference(probe, dims, self.ins, self.R, self.T)

    def call(self, fn):
        fn(self.c_dims, self.c_ins, self.out)


def setup_probe(probe):
    probe.mlg_fill_scaled.argtypes = [F32P, ctypes.c_long, ctypes.c_uint32, ctypes.c_float]
    probe.mlg_poison.argtypes = [F32P, ctypes.c_long]
    probe.mlg_gemm_ref.argtypes = [ctypes.c_int] * 3 + [F32P, F32P, F64P, F64P]
    probe.mlg_attn_ref.argtypes = [ctypes.c_int] * 2 + [F32P, F32P, F32P, F64P, F64P]
    probe.mlg_check_tol.restype = ctypes.c_double
    probe.mlg_check_tol.argtypes = [ctypes.c_long, F32P, F64P, F64P]


def judge(probe, fn, cases):
    ratios = []
    for c in cases:
        probe.mlg_poison(c.out, c.nout)
        c.call(fn)
        ratios.append(probe.mlg_check_tol(c.nout, c.out, c.R, c.T))
        if ratios[-1] > 1:
            return False, ratios
    return True, ratios


def load_kernel(so):
    fn = ctypes.CDLL(so).mlg_kernel
    fn.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(F32P), F32P]
    fn.restype = None
    return fn


def timed(fn, case):
    t0 = time.perf_counter()
    case.call(fn)
    return time.perf_counter() - t0


def bench(fn, case, budget):
    """Amostras cruas (s) de chamadas completas, incluindo threads; 1 aquecimento descartado."""
    case.call(fn)
    samples, total = [], 0.0
    while (total < budget or len(samples) < 5) and len(samples) < 200:
        samples.append(timed(fn, case))
        total += samples[-1]
    return samples


def stats(samples):
    s = sorted(samples)
    p95 = s[min(len(s) - 1, int(round(0.95 * (len(s) - 1))))]
    return dict(median_s=statistics.median(s), best_s=s[0], p95_s=p95, worst_s=s[-1],
                cv=statistics.pstdev(s) / statistics.mean(s), n_samples=len(s))


def duel(fa, fb, case, rounds=9, per_round=5):
    ta, tb = [], []
    for _ in range(rounds):
        ta.append(statistics.median([timed(fa, case) for _ in range(per_round)]))
        tb.append(statistics.median([timed(fb, case) for _ in range(per_round)]))
    return ta, tb


# --------------------------------------------------------------------------- relatório de fronteira

def human(x, unit):
    for p, s in ((1e12, "T"), (1e9, "G"), (1e6, "M"), (1e3, "k")):
        if abs(x) >= p:
            return "%.2f %s%s" % (x / p, s, unit)
    return "%.2f %s" % (x, unit)


def ms(x):
    return "%.3f ms" % (x * 1e3)


def frontier(op, dims, dna, champ, baseline, args):
    t = champ["stats"]["median_s"]
    flops, nbytes = op.flops(dims), op.bytes_min(dims)
    t_compute = flops / dna["peak_all"]
    t_memory = nbytes / dna["bw_all"]
    t_lb = max(t_compute, t_memory)
    wall = "CÁLCULO" if t_compute >= t_memory else "MEMÓRIA"
    eff = t_lb / t
    g = champ["genome"]
    rep = dict(observed_median_s=t, observed_p95_s=champ["stats"]["p95_s"], compute_lb_s=t_compute,
               memory_lb_s=t_memory, dominant_wall=wall, efficiency_upper_bound=eff,
               remaining_at_least=1 / eff, flops=flops, bytes_min=nbytes,
               champion_bytes_model=op.bytes_model(g, dims), champion_ram_extra=op.ram_extra(g, dims, args.cpus))

    print("\n" + "=" * 78)
    print("  MLG FRONTIER REPORT — %s %s" % (op.name, "x".join(map(str, dims))))
    print("  spec: %s" % op.spec)
    print("=" * 78)
    print("  observado (mediana)      : %s   p95 %s" % (ms(t), ms(champ["stats"]["p95_s"])))
    if baseline:
        tb = baseline["stats"]["median_s"]
        rep["baseline_median_s"], rep["speedup_vs_baseline"] = tb, tb / t
        print("  linha de base (%-9s): %s   speedup %.2fx" % (op.describe(baseline["genome"]), ms(tb), tb / t))
    else:
        print("  linha de base            : inviável sob este modelo de custo (ex.: orçamento de RAM)")
    print("  limite de cálculo        : %s  (%s / %s empírico)" % (ms(t_compute), human(flops, "FLOP"),
                                                                 human(dna["peak_all"], "FLOP/s")))
    if op.transcendentals(dims):
        print("                             + %s exp() não contadas como FLOP" % human(op.transcendentals(dims), ""))
    print("  limite de memória        : %s  (%s mínimos / %s DRAM)" % (ms(t_memory), human(nbytes, "B"),
                                                                     human(dna["bw_all"], "B/s")))
    print("  parede dominante         : %s" % wall)
    print("  eficiência               : <= %.1f%% da parede  ->  folga >= %.2fx sob este contrato"
          % (100 * eff, 1 / eff))
    print("  campeão                  : %s" % genome_str(op, g))
    print("    bytes no modelo        : %s   (pior caso, intermediários fora do cache; mínimo da operação %s)"
          % (human(rep["champion_bytes_model"], "B"), human(nbytes, "B")))
    print("    memória extra          : %s" % human(rep["champion_ram_extra"], "B"))
    ev = op.evidence(g)
    print("    evidência              : binário %s | algoritmo %s | numérico %s"
          % (LEVELS[champ["level"]].split()[0], ev["algorithm"].split(":")[0], ev["numeric"].split(":")[0]))

    print("\n  VEREDITO")
    if eff >= 0.75:
        if wall == "CÁLCULO":
            verdict = ("Pare de otimizar este kernel: ele já está a >= 75% da parede de cálculo. "
                       "O próximo avanço exige fazer menos operações (matemática) ou outro silício.")
        else:
            verdict = ("Pare de mexer na matemática: a operação está limitada pela memória. "
                       "O próximo avanço exige mover menos bytes (representação, fusão) ou mais banda.")
    elif wall == "CÁLCULO":
        verdict = ("A parede é de cálculo e o campeão está a <= %.0f%% dela: o ganho ainda está na "
                   "IMPLEMENTAÇÃO (até %.1fx). Não adianta mexer em bytes." % (100 * eff, 1 / eff))
    else:
        verdict = ("A parede é de memória e o campeão está a <= %.0f%% dela: o ganho ainda está no "
                   "movimento de dados da implementação (até %.1fx)." % (100 * eff, 1 / eff))
    print("  " + verdict)
    rep["verdict"] = verdict
    moves = (["reduzir operações (outro algoritmo)", "outro silício / mais núcleos / ISA mais larga"]
             if wall == "CÁLCULO" else
             ["reduzir bytes (precisão menor, esparsidade, fusão)", "mais banda (outro silício)"])
    print("  como mover a fronteira   : " + "; ".join(moves))
    rep["frontier_moves"] = moves

    if args.link_mbps:
        link = args.link_mbps * 1e6 / 8
        rtt = args.link_ms / 1e3
        t_remote = (t / 2) / args.remote_speed
        t_comm = op.split_bytes(dims) / link
        t_dist = max(t / 2, t_remote) + t_comm + rtt
        change = t_dist / t - 1
        rep["distribution"] = dict(local_s=t, distributed_s=t_dist, compute_s=max(t / 2, t_remote),
                                   communication_s=t_comm, synchronization_s=rtt, change=change)
        print("\n  DISTRIBUIR EM 2 APARELHOS (link %.0f Mbit/s, RTT %.1f ms, remoto %.2fx)"
              % (args.link_mbps, args.link_ms, args.remote_speed))
        print("  T = compute %s + comunicação %s + sincronização %s = %s  (local %s)"
              % (ms(max(t / 2, t_remote)), ms(t_comm), ms(rtt), ms(t_dist), ms(t)))
        if change > 0:
            print("  -> Distribuir PIORA %.0f%%: a comunicação domina. Recusado." % (100 * change))
        else:
            print("  -> Distribuir melhora %.0f%%." % (-100 * change))
    print("=" * 78)
    return rep


# --------------------------------------------------------------------------- laço

def run_op(op, args, ctx):
    """Busca completa para uma operação. ctx: probe, flags, isas, dna, rng, state, fingerprint."""
    probe, flags, isas, rng = ctx["probe"], ctx["flags"], ctx["isas"], ctx["rng"]
    dims = op.bench_dims(args)
    shape_key = "x".join(map(str, dims))
    print("\n" + "#" * 78)
    print("# OPERAÇÃO: %s  (%s)" % (op.name, shape_key))
    print("# SPEC: %s" % op.spec)
    print("#" * 78)

    gate = [Case(op, probe, c["dims"], c["scales"], i + 1, True, c.get("label"))
            for i, c in enumerate(op.gate_cases(args))]
    bcase = Case(op, probe, dims, [1.0] * len(op.inputs), 99, False)

    synth = Synth(op, args.cpus, rng, args.rewrite_rate, isas)
    champions = ctx["state"].setdefault("champions", {})
    mine = champions.setdefault(ctx["fingerprint"], {}).setdefault(op.name, {})
    incumbent = mine.get(shape_key)
    prior = []
    for fp, ops in champions.items():
        for sk, rec in (ops.get(op.name, {}) if isinstance(ops, dict) else {}).items():
            g = synth.sanitize(rec["genome"])
            if g:
                prior.append(g)
    if prior:
        print("  prior: %d genoma(s) campeões do histórico entram na população inicial" % len(prior))
    population = synth.seed_population(args.cpus, args.pop, prior)
    if incumbent:
        print("  campeão anterior (%s) vai ter que se provar de novo" % ms(incumbent["median_s"]))

    results = {}
    for gen in range(args.gens):
        population = [g for g in {genome_key(g): g for g in population}.values() if genome_key(g) not in results]
        print("\n  [geração %d] %d candidatos" % (gen, len(population)))
        built = build_candidates(op, population, flags, isas, args.cpus)
        for g in population:
            art = built[genome_key(g)]
            rec = dict(genome=g, generation=gen, ok=False, level=0, artifact=art, evidence=op.evidence(g),
                       ram_extra=op.ram_extra(g, dims, args.cpus))
            results[genome_key(g)] = rec
            if args.ram_mb and rec["ram_extra"] > args.ram_mb * 1024 ** 2:
                rec["why"] = "inviável: precisa de %s extras, orçamento %s" % (
                    human(rec["ram_extra"], "B"), human(args.ram_mb * 1024 ** 2, "B"))
                print("    INVIÁVEL   %-46s %s" % (genome_str(op, g), rec["why"]))
                continue
            if not art["so"]:
                rec["why"] = "não compilou: " + art["error"]
                print("    NÃO COMPILOU %-44s %s" % (genome_str(op, g), art["error"][:60]))
                continue
            fn = load_kernel(art["so"])
            env = fp_env(probe)
            rec["contract"] = dict(
                dtype="float32", rounding=env["rounding"], ftz=env["ftz"], daz=env["daz"],
                fma=None if art["fma_instr"] is None else art["fma_instr"] > 0,
                reassociation="allowed (-ffast-math)" if g.get("FM") else "compiler-default",
                assumptions=op.assumptions(g), compiler=compiler_version(), flags=art["flags"],
                isa=platform.machine() + "/" + g.get("ISA", "default"))
            if env["rounding"] != "nearest":
                rec["why"] = "contrato violado: arredondamento %s" % env["rounding"]
                print("    REJEITADO  %-46s %s" % (genome_str(op, g), rec["why"]))
                continue
            ok, ratios = judge(probe, fn, gate)
            rec["gate_ratios"] = dict(zip([c.label for c in gate], ratios))
            if not ok:
                rec["why"] = "contraexemplo: erro %.3gx a tolerância em %s" % (ratios[-1], gate[len(ratios) - 1].label)
                print("    REJEITADO  %-46s %s" % (genome_str(op, g), rec["why"]))
                continue
            rec["ok"], rec["level"] = True, 2
            rec["samples_s"] = bench(fn, bcase, args.budget)
            rec["stats"] = st = stats(rec["samples_s"])
            print("    ok %10s (cv %4.1f%%)  %-44s erro/tol=%.3f" % (
                ms(st["median_s"]), 100 * st["cv"], genome_str(op, g), max(ratios)))
        ranked = sorted((r for r in results.values() if r["ok"]), key=lambda r: r["stats"]["median_s"])
        parents = [r["genome"] for r in ranked[:4]] or [synth.random()]
        population = [synth.mutate(rng.choice(parents)) for _ in range(args.pop)]

    ranked = sorted((r for r in results.values() if r["ok"] and r["level"] >= PROMOTION_MIN_LEVEL),
                    key=lambda r: r["stats"]["median_s"])
    winner = ranked[0]
    baseline = next((r for r in ranked if r["genome"]["kind"] == op.baseline_kind and r["genome"]["T"] == 1
                     and not r["genome"].get("FM")), None)
    rejected = [r for r in results.values() if not r["ok"]]
    print("\n  seleção: %d avaliados, %d reprovados; vencedor %s (%s)" % (
        len(results), len(rejected), genome_str(op, winner["genome"]), ms(winner["stats"]["median_s"])))

    champ, duel_rec = winner, None
    if incumbent and genome_key(incumbent["genome"]) != genome_key(winner["genome"]):
        inc = results.get(genome_key(synth.sanitize(incumbent["genome"]) or {}))
        if inc and inc["ok"]:
            ti, tw = duel(load_kernel(inc["artifact"]["so"]), load_kernel(winner["artifact"]["so"]), bcase)
            gain = statistics.median(ti) / statistics.median(tw)
            need = 1 + max(0.02, 2 * max(stats(ti)["cv"], stats(tw)["cv"]))
            duel_rec = dict(incumbent_s=ti, challenger_s=tw, gain=gain, required=need)
            print("  duelo intercalado: desafiante %.3fx o campeão (exigido > %.3fx)" % (gain, need))
            if gain <= need:
                champ = inc
                print("  não venceu além do ruído — campeão mantido (rollback)")
        else:
            print("  campeão anterior REPROVOU ou não existe mais neste espaço — destituído")
    mine[shape_key] = dict(genome=champ["genome"], median_s=champ["stats"]["median_s"],
                           so_sha256=champ["artifact"]["so_sha256"], level=champ["level"],
                           when=time.strftime("%Y-%m-%d %H:%M:%S"))

    rep = frontier(op, dims, ctx["dna"], champ, baseline, args)
    return dict(op=op.name, spec=op.spec, dims=dims, gate_cases=[c.label for c in gate],
                candidates=list(results.values()), champion=genome_key(champ["genome"]),
                duel=duel_rec, frontier=rep)

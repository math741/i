"""Atenção: Y = softmax(Q·Kᵀ/√D)·V. Mesma função, grafo computacional diferente.

O espaço de transformações contém três formas de calcular a mesma função:
  full    materializa QKᵀ inteiro (S×S floats), depois softmax, depois P·V
  row     uma linha de scores por vez (S floats por thread)
  online  softmax online em blocos BR×BC: QKᵀ nunca existe; memória O(BR·BC)
O motor não recebe "use FlashAttention": recebe a identidade do softmax online como
regra admissível (teorema em ℝ) e escolhe, pela medição, se e como usá-la.
"""

HEAD = r"""
#define D %(D)d
static inline float dot(const float *restrict a, const float *restrict b) {
    float s = 0.f;
    for (int c = 0; c < D; c++) s += a[c] * b[c];
    return s;
}
static void rows(const int *dims, const float *const *in, float *out, int r0, int r1) {
    const int S = dims[0];
    const float *restrict Q = in[0], *restrict Kx = in[1], *restrict V = in[2];
    float *restrict Y = out;
    const float sc = 1.0f / sqrtf((float)D);
"""

FULL = HEAD + r"""
    float *restrict P = g_scratch; /* S×S: QKᵀ materializado */
    for (int i = r0; i < r1; i++)
        for (int j = 0; j < S; j++) P[(long)i * S + j] = dot(Q + (long)i * D, Kx + (long)j * D) * sc;
    for (int i = r0; i < r1; i++) {
        float *p = P + (long)i * S, m = -FLT_MAX, l = 0.f;
        for (int j = 0; j < S; j++) if (p[j] > m) m = p[j];
        %(MAXSUB)s
        for (int j = 0; j < S; j++) { p[j] = expf(p[j] - m); l += p[j]; }
        const float inv = 1.0f / l;
        for (int j = 0; j < S; j++) p[j] *= inv;
    }
    for (int i = r0; i < r1; i++) {
        float y[D];
        for (int c = 0; c < D; c++) y[c] = 0.f;
        for (int j = 0; j < S; j++) {
            const float pj = P[(long)i * S + j];
            const float *restrict v = V + (long)j * D;
            for (int c = 0; c < D; c++) y[c] += pj * v[c];
        }
        for (int c = 0; c < D; c++) Y[(long)i * D + c] = y[c];
    }
}
"""

ROW = HEAD + r"""
    float *p = malloc(sizeof(float) * (S > 0 ? S : 1));
    for (int i = r0; i < r1; i++) {
        float m = -FLT_MAX, l = 0.f, y[D];
        for (int j = 0; j < S; j++) {
            p[j] = dot(Q + (long)i * D, Kx + (long)j * D) * sc;
            if (p[j] > m) m = p[j];
        }
        %(MAXSUB)s
        for (int c = 0; c < D; c++) y[c] = 0.f;
        for (int j = 0; j < S; j++) {
            const float w = expf(p[j] - m);
            const float *restrict v = V + (long)j * D;
            l += w;
            for (int c = 0; c < D; c++) y[c] += w * v[c];
        }
        const float inv = 1.0f / l;
        for (int c = 0; c < D; c++) Y[(long)i * D + c] = y[c] * inv;
    }
    free(p);
}
"""

ONLINE = r"""
#define BR %(BR)d
#define BC %(BC)d
""" + HEAD + r"""
    for (int i0 = r0; i0 < r1; i0 += BR) {
        const int nb = r1 - i0 < BR ? r1 - i0 : BR;
        float o[BR][D], m[BR], l[BR], s[BR][BC];
        for (int a = 0; a < nb; a++) {
            m[a] = -FLT_MAX;
            l[a] = 0.f;
            for (int c = 0; c < D; c++) o[a][c] = 0.f;
        }
        for (int j0 = 0; j0 < S; j0 += BC) {
            const int nc = S - j0 < BC ? S - j0 : BC;
            for (int a = 0; a < nb; a++)
                for (int b = 0; b < nc; b++)
                    s[a][b] = dot(Q + (long)(i0 + a) * D, Kx + (long)(j0 + b) * D) * sc;
            for (int a = 0; a < nb; a++) {
                float mb = m[a];
                for (int b = 0; b < nc; b++) if (s[a][b] > mb) mb = s[a][b];
                const float alpha = %(ALPHA)s;
                l[a] *= alpha;
                for (int c = 0; c < D; c++) o[a][c] *= alpha;
                for (int b = 0; b < nc; b++) {
                    const float w = expf(s[a][b] - mb);
                    const float *restrict v = V + (long)(j0 + b) * D;
                    l[a] += w;
                    for (int c = 0; c < D; c++) o[a][c] += w * v[c];
                }
                m[a] = mb;
            }
        }
        for (int a = 0; a < nb; a++) {
            const float inv = 1.0f / l[a];
            for (int c = 0; c < D; c++) Y[(long)(i0 + a) * D + c] = o[a][c] * inv;
        }
    }
}
"""


class Attention:
    name = "attention"
    spec = "Y[S×D] = softmax(Q·Kᵀ/√D)·V, Q,K,V: S×D (uma cabeça, não causal)"
    baseline_kind = "full"
    inputs = ("Q", "K", "V")

    kinds = {
        "full": {},
        "row": {},
        "online": dict(BR=[1, 2, 4, 8, 16], BC=[16, 32, 64, 128, 256]),
    }
    kind_weights = {"full": 1, "row": 1, "online": 3}
    rewrites = {
        "full": {"no_max_subtract": dict(MAXSUB="m = 0.f; /* rewrite: subtrair o máximo é redundante em ℝ */")},
        "row": {"no_max_subtract": dict(MAXSUB="m = 0.f; /* rewrite: subtrair o máximo é redundante em ℝ */")},
        "online": {"skip_rescale": dict(ALPHA="1.0f /* rewrite: reescala considerada desnecessária */")},
    }

    def __init__(self):
        self.d = 64

    def bench_dims(self, args):
        self.d = args.d
        return (args.seq, args.d)

    def gate_cases(self, args):
        self.d = args.d
        S = args.seq
        cases = [dict(dims=(s, args.d), scales=(1.0, 1.0, 1.0)) for s in (1, 7, 33, 100, 257, S + 3)]
        # logits grandes: Q×256 => scores na casa das centenas; exp sem subtrair o máximo estoura.
        cases.append(dict(dims=(129, args.d), scales=(256.0, 1.0, 1.0), label="logits-grandes"))
        return cases

    def input_sizes(self, dims):
        S, D = dims
        return [S * D, S * D, S * D], S * D

    def reference(self, probe, dims, ins, R, T):
        S, D = dims
        probe.mlg_attn_ref(S, D, ins[0], ins[1], ins[2], R, T)

    def source(self, g):
        p = dict(D=self.d, BR=g.get("BR", 1), BC=g.get("BC", 1), MAXSUB="", ALPHA="expf(m[a] - mb)")
        p.update(self.rewrites.get(g["kind"], {}).get(g.get("rewrite"), {}))
        body = {"full": FULL, "row": ROW, "online": ONLINE}[g["kind"]]
        scratch = "(long)dims[0] * dims[0]" if g["kind"] == "full" else "0"
        return body % p, dict(RALIGN=p["BR"], SCRATCH=scratch)

    def describe(self, g):
        if g["kind"] == "online":
            return "online BR=%d BC=%d" % (g["BR"], g["BC"])
        return g["kind"]

    def evidence(self, g):
        if g.get("rewrite"):
            return dict(algorithm="E0: reescrita sem prova", numeric="análise de 1ª ordem")
        alg = {"full": "E4: é a própria definição (nada foi transformado)",
               "row": "E4: mesma definição, linha a linha (exato em ℝ)",
               "online": "E4: identidade do softmax online (teorema em ℝ, qualquer blocagem)"}[g["kind"]]
        return dict(algorithm=alg, numeric="E2: tolerância por análise de 1ª ordem (não teorema)")

    def assumptions(self, g):
        return ["expf com erro ≤ 8 ulp (libm/libmvec) — HIPÓTESE", "arredondamento ao mais próximo (medido)",
                "D = %d especializado em tempo de compilação" % self.d]

    def flops(self, dims):
        S, D = dims
        return 4.0 * S * S * D  # QKᵀ (2S²D) + P·V (2S²D); exp contado à parte

    def transcendentals(self, dims):
        S, _ = dims
        return float(S * S)

    def bytes_min(self, dims):
        S, D = dims
        return 4.0 * 4 * S * D  # ler Q, K, V e escrever Y uma vez

    def bytes_model(self, g, dims):
        S, _ = dims
        extra = 7 * 4.0 * S * S if g["kind"] == "full" else 0  # escreve/relê QKᵀ e P em ~7 passadas
        return self.bytes_min(dims) + extra

    def ram_extra(self, g, dims, cpus):
        S, D = dims
        T = min(g["T"], cpus)
        if g["kind"] == "full":
            return 4.0 * S * S
        if g["kind"] == "row":
            return 4.0 * T * S
        return 4.0 * T * g["BR"] * (g["BC"] + D + 2)

    def split_bytes(self, dims):
        """Dividir as consultas entre 2 aparelhos: metade de Q + K e V inteiros vão, metade de Y volta."""
        S, D = dims
        return 4.0 * (S / 2 * D + 2 * S * D + S / 2 * D)

    def seeds(self, cpus):
        return [dict(kind="full", T=1, FM=0, ISA="default"),
                dict(kind="row", T=cpus, FM=0, ISA="default"),
                dict(kind="online", T=cpus, BR=4, BC=64, FM=0, ISA="default")]

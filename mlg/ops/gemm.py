"""GEMM: C = A·B. Mesma matemática, implementação melhor."""

SIG = r"""
static void rows(const int *dims, const float *const *in, float *out, int r0, int r1) {
    const int N = dims[1], K = dims[2];
    const float *restrict A = in[0], *restrict B = in[1];
    float *restrict C = out;
"""

NAIVE = SIG + r"""
    for (int i = r0; i < r1; i++)
        for (int j = 0; j < N; j++) {
            float s = 0.f;
            for (int k = 0; k < K; k++) s += A[(long)i * K + k] * B[(long)k * N + j];
            C[(long)i * N + j] = s;
        }
}
"""

IKJ = SIG + r"""
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
#define MR %(MR)d
#define NR %(NR)d
#define KCV %(KC)d
""" + SIG + r"""
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


class Gemm:
    name = "gemm"
    spec = "C[M×N] = A[M×K] · B[K×N]"
    baseline_kind = "naive"
    inputs = ("A", "B")

    # TRANSFORMATION SPACE: genes por tipo de implementação (T, FM, ISA são comuns a todos).
    kinds = {
        "naive": {},
        "ikj": {},
        "block": dict(MR=[1, 2, 3, 4, 6, 8], NR=[4, 8, 12, 16, 24, 32, 48, 64], KC=[0, 32, 64, 128, 256, 512]),
    }
    kind_weights = {"naive": 0, "ikj": 0, "block": 1}
    # Reescritas propostas SEM prova. O motor não sabe se estão certas; só o gate decide.
    rewrites = {
        "block": {
            "skip_col_edge": dict(EDGE="/* rewrite: borda de colunas considerada redundante */"),
            "kc_trim_last": dict(K_REWRITE="if (k1 == K && k1 - k0 > 1) k1--; /* rewrite: último passo fundido */"),
        },
    }

    def bench_dims(self, args):
        return (args.n, args.n, args.n)

    def gate_cases(self, args):
        n = args.n
        return [dict(dims=d, scales=(1.0, 1.0)) for d in
                [(1, 1, 1), (7, 5, 13), (33, 65, 17), (64, 64, 64), (97, 40, 130), (n + 3, n - 5, n + 1)]]

    def input_sizes(self, dims):
        M, N, K = dims
        return [M * K, K * N], M * N

    def reference(self, probe, dims, ins, R, T):
        M, N, K = dims
        probe.mlg_gemm_ref(M, N, K, ins[0], ins[1], R, T)

    def source(self, g):
        p = dict(MR=g.get("MR", 1), NR=g.get("NR", 1), KC=g.get("KC", 0), EDGE=EDGE_OK, K_REWRITE="")
        p.update(self.rewrites.get(g["kind"], {}).get(g.get("rewrite"), {}))
        body = {"naive": NAIVE, "ikj": IKJ, "block": BLOCK}[g["kind"]]
        return body % p, dict(RALIGN=p["MR"], SCRATCH="0")

    def describe(self, g):
        if g["kind"] == "block":
            return "block MR=%d NR=%d KC=%s" % (g["MR"], g["NR"], g["KC"] or "K")
        return g["kind"]

    # EVIDENCE OBLIGATIONS
    def evidence(self, g):
        if g.get("rewrite"):
            return dict(algorithm="E0: reescrita sem prova", numeric="γ_K (Higham §3.1), teorema")
        return dict(algorithm="E4: só reordena/reagrupa a soma de K produtos (exato em ℝ)",
                    numeric="E3 p/ o algoritmo: |erro| ≤ γ_K·|A||B| p/ qualquer ordem (teorema)")

    def assumptions(self, g):
        return ["arredondamento ao mais próximo (medido)", "sem underflow/overflow (provado pelo domínio de entrada)"]

    # COST MODEL
    def flops(self, dims):
        M, N, K = dims
        return 2.0 * M * N * K

    def transcendentals(self, dims):
        return 0

    def bytes_min(self, dims):
        M, N, K = dims
        return 4.0 * (M * K + K * N + M * N)

    def bytes_model(self, g, dims):
        return self.bytes_min(dims)

    def ram_extra(self, g, dims, cpus):
        return 0

    def split_bytes(self, dims):
        """Dividir as linhas entre 2 aparelhos: metade de A + B inteira vão, metade de C volta."""
        M, N, K = dims
        return 4.0 * (M / 2 * K + K * N + M / 2 * N)

    def seeds(self, cpus):
        return [dict(kind="naive", T=1, FM=0, ISA="default"),
                dict(kind="ikj", T=1, FM=0, ISA="default"),
                dict(kind="block", T=cpus, MR=4, NR=16, KC=256, FM=0, ISA="default")]

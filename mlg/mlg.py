#!/usr/bin/env python3
"""MLG: máquina experimental de computação.

Entrada por operação: SPEC + TRANSFORMATION SPACE + EVIDENCE OBLIGATIONS + COST MODEL (ops/).
Laço: candidato -> deriva -> compila -> verifica -> mede -> modela o teto -> compara -> promove/rejeita.
Saída: campeão verificado + MLG FRONTIER REPORT (qual parede impede o próximo avanço)
       + relatório bruto em reports/<máquina>-<data>.json com todas as amostras.

Uso:  python3 mlg/mlg.py [gemm|attention|all] [--quick] [--link-mbps 20 --link-ms 5]
Só depende de python3 e de um compilador C. Roda no Pi Zero 2 W.
"""
import argparse
import ctypes
import json
import os
import platform
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import engine  # noqa: E402
import hw  # noqa: E402
from ops import OPS  # noqa: E402

REPORTS = os.path.join(hw.HERE, "reports")
STATE = os.path.join(hw.HERE, "mlg_state.json")


def main():
    ap = argparse.ArgumentParser(description="MLG: síntese + verificação + medição + fronteira")
    ap.add_argument("ops", nargs="*", default=["all"], help="gemm, attention ou all")
    ap.add_argument("--n", type=int, default=512, help="GEMM: tamanho n×n×n")
    ap.add_argument("--seq", type=int, default=1024, help="atenção: comprimento S")
    ap.add_argument("--d", type=int, default=64, help="atenção: dimensão da cabeça D")
    ap.add_argument("--gens", type=int, default=4, help="gerações de busca")
    ap.add_argument("--pop", type=int, default=12, help="candidatos por geração")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--budget", type=float, default=0.3, help="segundos de benchmark por candidato")
    ap.add_argument("--rewrite-rate", type=float, default=0.15, help="chance de reescrita sem prova")
    ap.add_argument("--bw-mb", type=float, default=0, help="força o conjunto de trabalho do teste de banda (MB)")
    ap.add_argument("--link-mbps", type=float, default=0, help="banda do link até o 2º aparelho (ex.: 20)")
    ap.add_argument("--link-ms", type=float, default=5.0, help="RTT do link até o 2º aparelho")
    ap.add_argument("--remote-speed", type=float, default=1.0, help="velocidade do 2º aparelho relativa a este")
    ap.add_argument("--ram-mb", type=float, default=0, help="orçamento de memória extra por chamada (MB); 0 = sem limite")
    ap.add_argument("--quick", action="store_true", help="busca curta")
    args = ap.parse_args()
    if args.quick:
        args.gens, args.pop, args.budget = 2, 8, 0.15
    names = list(OPS) if "all" in args.ops else args.ops
    for n in names:
        if n not in OPS:
            sys.exit("operação desconhecida: %s (disponíveis: %s)" % (n, ", ".join(OPS)))
    args.cpus = os.cpu_count() or 1
    seed = args.seed if args.seed is not None else random.randrange(1 << 30)

    flags = hw.detect_flags()
    isas = hw.isa_variants(flags)
    fingerprint = "%s | %s | %d cpus | %s MB" % (platform.machine(), hw.cpu_model(), args.cpus,
                                                  hw.meminfo_mb("MemTotal"))
    print("MLG :: território = %s" % fingerprint)
    print("     compilador = %s" % hw.compiler_version())
    print("     flags = %s   variantes de ISA = %s" % (" ".join(flags), ", ".join(sorted(isas))))
    probe_src = os.path.join(hw.HERE, "probe.c")
    ok, err = hw.compile_so([probe_src], os.path.join(hw.BUILD, "probe.so"), flags)
    if not ok:
        sys.exit(err)
    probe = ctypes.CDLL(os.path.join(hw.BUILD, "probe.so"))
    engine.setup_probe(probe)
    trust = hw.sha256(probe_src)
    print("     raiz de confiança (probe.c) sha256 = %s" % trust[:16])

    print("\n[DNA do hardware, medido agora]")
    dna = hw.probe_hardware(probe, flags, isas, args.cpus, args)
    print("  P_compute %s (1 thread %s, via ACC=%d %s)   frequência %.2f GHz" % (
        engine.human(dna["peak_all"], "FLOP/s"), engine.human(dna["peak_1t"], "FLOP/s"),
        dna["peak_from"]["acc"], dna["peak_from"]["isa"], dna["freq_all_hz"] / 1e9))
    print("  P_memory  %s DRAM (conjunto %s; dentro do L%d: %s)" % (
        engine.human(dna["bw_all"], "B/s"), engine.human(dna["bw_working_set"], "B"), dna["llc_level"],
        engine.human(dna.get("bw_llc_all", 0), "B/s")))

    state = {}
    if os.path.exists(STATE):
        with open(STATE) as f:
            state = json.load(f)
    ctx = dict(probe=probe, flags=flags, isas=isas, dna=dna, rng=random.Random(seed), state=state,
               fingerprint=fingerprint)
    results = [engine.run_op(OPS[n], args, ctx) for n in names]
    with open(STATE, "w") as f:
        json.dump(state, f, indent=2, sort_keys=True)

    os.makedirs(REPORTS, exist_ok=True)
    path = os.path.join(REPORTS, "%s-%s.json" % (platform.node() or "host", time.strftime("%Y%m%d-%H%M%S")))
    raw = dict(
        schema="mlg-report/2", when=time.strftime("%Y-%m-%dT%H:%M:%S%z"), argv=sys.argv[1:], seed=seed,
        fingerprint=fingerprint, python=sys.version.split()[0], compiler=hw.compiler_version(), flags=flags,
        isa_variants=isas, evidence_levels=engine.LEVELS,
        trust_root={"probe.c_sha256": trust, "promotion_min_level": engine.PROMOTION_MIN_LEVEL},
        methodology=dict(
            timer="time.perf_counter, wall clock, per full call incl. thread create/join",
            warmup="1 discarded call per candidate", ranking="median", cache="hot (inputs reused)",
            promotion="interleaved duel, gain > max(2%, 2*cv), level >= E2",
            ceilings="empirical: compute = max over probe variants (ACC x ISA); memory = STREAM triad "
                     "24 B/iter, working set >= 4x LLC; true ceilings are >= these",
            distribution="T = max(T/2, T/2/remote_speed) + split_bytes/link + RTT"),
        dna=dna, operations=results)
    with open(path, "w") as f:
        json.dump(raw, f, indent=1, sort_keys=True)
    print("\nrelatório bruto: %s" % os.path.relpath(path, os.getcwd()))


if __name__ == "__main__":
    main()

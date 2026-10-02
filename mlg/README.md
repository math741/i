# MLG — semente rodando em silício real

O laço central da MLG, completo e executável. Não é uma versão menor da ideia: é o mecanismo inteiro
(síntese → verificação com prova → medição → seleção → rollback) aplicado a uma operação (matmul FP32)
em hardware real.

```
hardware real ─► DNA medido ─► síntese de candidatos ─► GATE (prova) ─► benchmark ─► campeão
      ▲                                                                                   │
      └──────────────────────── próxima execução tem que vencer o campeão ◄───────────────┘
```

## Rodar

Precisa só de `python3` e de um compilador C. Funciona em x86, ARM64 e ARMv7 (Pi Zero 2 W).

```sh
python3 mlg/mlg.py                                    # busca completa
python3 mlg/mlg.py --quick                            # busca curta (Pi)
python3 mlg/mlg.py --link-mbps 20 --link-ms 5 --watts 1.5   # + tetos de rede e Landauer
```

No Pi Zero 2 W (512 MB) use `--n 256`; o teste de banda já se limita a 1/3 da memória livre.

## O que cada parte faz

| Peça | Arquivo | O que é de verdade |
|---|---|---|
| DNA do hardware | `probe.c`, `peak.c` | Teto de FLOP/s achado por **busca** (acumuladores × largura de vetor), banda DRAM com conjunto de trabalho ≥ 4× o último cache, banda dentro do cache (só referência), frequência efetiva sob carga, temperatura. |
| Motor de síntese | `mlg.py` (`Synth`, `gen_source`) | Genoma = tipo, bloco de registradores MR×NR, tiling KC, threads, contrato numérico (fast-math ou não) e **ISA** (ex.: vetores de 512 bits). Vira C, compila em paralelo, evolui por mutação dos melhores. |
| Reescritas não provadas | `REWRITES` | "Otimizações" que parecem válidas. O motor não sabe se estão certas; só o gate decide. |
| Contrato de prova | `mlg.py` (`contract`) | Cada candidato registra: dtype, arredondamento e FTZ/DAZ **medidos após carregar o binário**, FMA (contado no disassembly), reassociação, argumento de overflow/underflow, compilador, flags, ISA, hashes do fonte e do binário. |
| Gate | `mlg_ref`, `mlg_check` | Limite γ_K·(\|A\|·\|B\|) (Higham §3.1), válido para qualquer ordem de soma sob o contrato. Violar = **contraexemplo** (prova de bug). Passar = evidência **nível 2**, não prova. |
| Benchmark | `bench`, `stats` | Amostras cruas, 1 aquecimento descartado, ≥ 5 amostras, ranking por **mediana**, CV registrado. |
| Promoção / rollback | `duel`, `mlg_state.json` | Duelo intercalado campeão × desafiante; promove só com ganho > max(2%, 2·CV) e nível ≥ 2. Campeão que reprova é destituído. |
| Raiz de confiança | `probe.c` | O gate é pequeno, fica fora do espaço de busca e tem o sha256 gravado em todo relatório. |
| Relatório bruto | `reports/*.json` | Tudo: DNA com todas as variantes do probe, cada candidato com contrato, razões do gate por forma, todas as amostras de tempo, duelo, tetos. |

## Níveis de evidência

| Nível | Nome | Aqui |
|---|---|---|
| 4 | FORMAL | ainda não existe |
| 3 | CERTIFIED | ainda não existe |
| 2 | DIFFERENTIAL+BOUND | gate atual: referência em double + tolerância provada, formas que forçam bordas |
| 1 | PROPERTY | — |
| 0 | EXPERIMENTAL | nunca substitui um campeão |

## Metodologia do benchmark

- FLOP = 2n³ (algoritmo clássico). Tempo = relógio de parede por chamada completa, **incluindo** criar e juntar as threads.
- Cache quente: A, B e C (3n² floats) são reutilizados.
- Todo teto é **empírico**: a melhor medição de um probe. O teto verdadeiro é ≥ ele, então "% do teto" é um limite **superior** da eficiência.
- `P_reachable = min(P_compute, BW_DRAM × I)` diz qual parede está sendo atingida.
- Landauer aparece só como referência (kT·ln2 por bit apagado irreversivelmente), **não** como limite de J/FLOP.

## Errata (versão anterior desta semente)

1. **O teto de 299 GFLOP/s estava errado.** O probe tinha 64 acumuladores = 4 registradores zmm, menos que latência × portas de FMA, e media latência, não vazão. Além disso, a busca não tinha vetores de 512 bits. Teto corrigido nesta máquina: ~650 GFLOP/s (≈ 58 FLOP/ciclo/núcleo a 2,8 GHz, perto dos 64 teóricos do AVX-512 com 2 FMAs).
2. **A banda de 94 GB/s era do cache L3** (260 MiB nesta máquina), não da DRAM. DRAM real: ~45 GB/s. Os tetos de tokens/s caíram pela metade.
3. **208 GFLOP/s era o melhor caso**, não a mediana. Nesta VM a mediana do campeão fica entre 130 e 165 GFLOP/s, com dispersão grande. Logo, a eficiência real é ≤ 20–25% do teto, não 70%.
4. "Passar no gate" foi descrito como prova. É evidência nível 2; só a **reprovação** é prova (contraexemplo).
5. Landauer foi apresentado como teto de J/FLOP. Não é.

## Próximas operações no mesmo laço

Atenção fundida (sem materializar QKᵀ), matmul INT8/INT4 com limite de erro de quantização provado,
e o escalonador trocando o campeão quando a temperatura muda. Cada uma entra pelo mesmo gate.

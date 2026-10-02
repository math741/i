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
python3 mlg/mlg.py --link-mbps 20 --link-ms 5 --watts 1.5   # + tetos de rede e energia
```

No Pi Zero 2 W (512 MB) use `--n 256 --bw-mb 48` se a memória estiver apertada.

## O que cada parte faz

| Peça | Arquivo | O que é de verdade |
|---|---|---|
| DNA do hardware | `probe.c`, `peak.c` | Mede FLOP/s (1 e N threads), banda de memória (1 e N threads), temperatura. Descobre as flags de compilação que este compilador aceita aqui. |
| Motor de síntese | `mlg.py` (`Synth`, `gen_source`) | Cada candidato é um genoma (tipo, bloco de registradores MR×NR, tiling KC, threads, contrato numérico). Vira código C, é compilado em paralelo e carregado. Evolui por mutação dos melhores. |
| Reescritas não provadas | `REWRITES` | O sintetizador às vezes propõe "otimizações" agressivas que parecem válidas. Ele não sabe se estão certas. |
| Gate de verificação | `mlg_ref`, `mlg_check` | Teorema (Higham §3.1): qualquer ordem de soma em float de K produtos tem erro ≤ γ_K·(\|A\|·\|B\|). Toda reordenação, tiling, vetorização ou FMA correta cabe nesse limite; quem passa dele está **provadamente** errado. Testa formas escolhidas para quebrar bordas (1×1×1, primos, não múltiplos de bloco). |
| Autotuner | `bench` | Mede no silício, no estado físico atual. |
| Rollback | `mlg_state.json` | O campeão de cada máquina é salvo e tem que se provar de novo a cada execução. Um desafiante só é promovido se passar no gate **e** for >2% mais rápido. |
| Relatório do limite | `report_limits` | Quanto falta até o teto medido, e os tetos físicos de decodificação de um LLM, de rede e de energia. |

## Os muros que nenhum diagrama atravessa

Diagrama não tem teto: sempre dá para desenhar mais uma camada "meta". O limite real é onde a física
e a matemática dizem não. Esses não mudam com arquitetura de software:

1. **Roofline.** Um token de um LLM com batch 1 lê todos os pesos. `tokens/s ≤ banda / bytes_dos_pesos`.
   Nenhum compilador passa disso; só se mexe nos bytes (quantização, esparsidade) ou na banda (silício).
2. **Rede.** Pesos em outro aparelho via Wi-Fi: um modelo de 101M em INT8 leva dezenas de segundos por
   token. "Uma memória só" atravessando a rede funciona para dados frios, nunca para pesos quentes.
   O que viaja são ativações, e cada fronteira entre aparelhos custa pelo menos um RTT por token.
3. **Rice / parada.** Nenhum gate decide correção de código arbitrário. Verificação forte só existe em
   fragmentos com teorema (como o limite de erro usado aqui). Fora deles, o gate vira teste, não prova.
4. **Obstáculo de Löb.** Um sistema não prova a solidez de um sucessor tão forte quanto ele mesmo.
   Autoalteração ilimitada exige uma âncora de verificação que o próprio sistema não reescreve.
5. **No Free Lunch.** Busca sobre "todas as arquiteturas" não é melhor que outra em média: precisa de
   priors. O espaço de busca é uma decisão de projeto, não um detalhe.
6. **Landauer.** kT·ln2 ≈ 2,9·10⁻²¹ J por bit apagado a 300 K. Hardware atual está ~10¹⁰ acima disso.
   Esse é o teto absoluto; todo o caminho até ele é engenharia.

## Próximas operações no mesmo laço

Atenção fundida (sem materializar QKᵀ), matmul INT8/INT4 com limite de erro de quantização provado,
e o escalonador trocando o campeão quando a temperatura muda. Cada uma entra pelo mesmo gate.

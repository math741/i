# MLG — máquina experimental de computação

A MLG não é um supercompilador. Para cada operação, ela recebe quatro coisas:

```
OPERATION SPEC         o que matematicamente queremos
TRANSFORMATION SPACE   o que é permitido alterar
EVIDENCE OBLIGATIONS   o que precisa ser demonstrado
COST MODEL             o que significa "melhor"
```

e roda o mesmo laço para qualquer uma delas:

```
candidato -> deriva -> compila -> verifica -> mede -> modela o teto -> compara -> promove / rejeita
```

A saída não é só "vencedor: X". É um **MLG FRONTIER REPORT** com os limites inferiores de cálculo, memória
e comunicação, a parede dominante, a folga restante sob o contrato atual e um veredito sobre **qual parede
atacar**. Exemplos: "o ganho ainda está na implementação", "pare de mexer na matemática, o limite é memória",
"distribuir piora 12000%, recusado".

Regra máxima: toda promessa termina numa unidade mensurável ou numa obrigação demonstrável.

## Rodar

Precisa só de `python3` e de um compilador C. Funciona em x86, ARM64 e ARMv7 (Pi Zero 2 W).

```sh
python3 mlg/mlg.py                                  # gemm + atenção
python3 mlg/mlg.py attention --seq 4096 --ram-mb 16 # atenção com orçamento de RAM
python3 mlg/mlg.py all --link-mbps 20 --link-ms 5   # + veredito de distribuição em 2 aparelhos
python3 mlg/mlg.py all --quick --n 256 --seq 512    # Pi Zero 2 W
```

## Arquivos

| Arquivo | Papel |
|---|---|
| `probe.c` | **Raiz de confiança.** Referências em double + tolerância por operação, comparador único e sondas de hardware. Não evolui; o sha256 vai em todo relatório. |
| `peak.c` | Sonda do teto de FLOP/s (parametrizada; o teto é o máximo sobre as variantes). |
| `hw.py` | DNA do hardware: flags, variantes de ISA, teto de cálculo, banda DRAM (conjunto ≥ 4× o último cache), frequência, temperatura, ambiente de ponto flutuante. |
| `engine.py` | **O núcleo.** Não conhece nenhuma operação: síntese genérica, gate, benchmark, duelo, prior do histórico, relatório de fronteira e veredito de distribuição. |
| `ops/gemm.py` | GEMM: mesma matemática, implementação melhor. |
| `ops/attention.py` | Atenção: mesma função, grafo diferente (`full` materializa QKᵀ, `row` faz uma linha por vez, `online` nunca o cria). |
| `reports/*.json` | Relatórios brutos: todas as amostras, contratos, razões do gate e fronteira. |

Adicionar uma operação é escrever um arquivo em `ops/` com spec, espaço, obrigações e custo. O núcleo não muda.

## Escada de evidência

| Nível | Significado | Hoje |
|---|---|---|
| E5 | prova formal checada por kernel pequeno | — |
| E4 | transformação certificada (teorema aplicável) | nível do **algoritmo**: reordenar a soma (GEMM), identidade do softmax online (atenção) |
| E3 | erro máximo matematicamente limitado | GEMM, nível do algoritmo: γ_K (Higham §3.1) |
| E2 | referência + tolerância + formas adversariais | nível do **binário**, para todo candidato aprovado |
| E1 | validação empírica | — |
| E0 | experimental | reescritas sem prova; nunca substituem campeão de classe superior |

Cada candidato registra separadamente três evidências: a do algoritmo, a numérica e a do binário. O binário
fica em E2 porque provar que ele implementa o algoritmo analisado é o que o teorema de Rice impede em geral.
Reprovar no gate é **contraexemplo**, ou seja, prova de bug dado o contrato.

A tolerância da atenção vem de uma análise de **primeira ordem**, com a hipótese registrada de que `expf`
erra no máximo 8 ulp. Ela é frouxa: o erro observado fica em ~0,2–0,4% dela. Mesmo assim mata os bugs reais.
`skip_rescale` passa da tolerância por 7× a 685×. `no_max_subtract` passa nas entradas pequenas e morre no
caso de logits grandes, que está no gate exatamente para isso.

## Contrato de cada candidato

Cada candidato registra: dtype; arredondamento e FTZ/DAZ **medidos depois de carregar o binário**; FMA
contado no disassembly; reassociação; hipóteses da operação; compilador; flags; ISA; sha256 do fonte e do binário.

## Modelo de custo

- Os tetos são **empíricos**, e o teto verdadeiro é maior ou igual a eles. Por isso "% da parede" é um limite superior e "folga" é um limite inferior.
- Limite de cálculo = FLOP / P_compute; limite de memória = bytes mínimos / banda DRAM; a parede dominante é o maior dos dois.
- A memória extra de cada candidato é calculada; com `--ram-mb`, quem não cabe vira inviável.
- Distribuição: `T = max(T/2, T/2/v_remoto) + bytes_divididos/link + RTT`. Se piora, é recusada.
- O ranking usa a **mediana**. A promoção exige vencer um duelo intercalado com ganho > max(2%, 2·CV).
- Prior: campeões do histórico (outras formas, outras máquinas) entram na população inicial. É o embrião de P(estratégia boa | estrutura, hardware, histórico).

## Resultados nesta máquina (Xeon 4 núcleos, AVX-512; ver `reports/`)

| Operação | Campeão (mediana) | Linha de base | Parede | Eficiência | Veredito |
|---|---|---|---|---|---|
| GEMM 512³ | 1,03 ms (260 GFLOP/s) | naive 150 ms (145×) | cálculo | ≤ 39% | ganho ainda na implementação (≤ 2,6×) |
| Atenção S=1024 | 2,59 ms, **full** | full 1 thread 32,7 ms (12,6×) | cálculo | ≤ 16% | ganho ainda na implementação (≤ 6,4×) |
| Atenção S=4096, RAM ≤ 16 MB | 44,7 ms, **online**, 8 kB extras | full inviável (67 MB) | cálculo | ≤ 14% | ganho ainda na implementação (≤ 7,1×) |

Na atenção com S=1024, materializar QKᵀ (4 MB) **vence**, porque cabe no L3 de 260 MB. Não materializar só
vira vantagem quando a matriz não cabe no cache ou na RAM. Por isso a MLG não recebe a regra "use
FlashAttention": recebe a identidade como transformação admissível e a medição decide.
Distribuir qualquer uma das duas num link de 20 Mbit/s piora entre 100× e 800×.

## Errata (versão anterior desta semente)

1. **O teto de 299 GFLOP/s estava errado.** O probe tinha 64 acumuladores = 4 registradores zmm, menos que latência × portas de FMA, e media latência, não vazão. Além disso, a busca não tinha vetores de 512 bits. Teto corrigido nesta máquina: ~650 GFLOP/s (≈ 58 FLOP/ciclo/núcleo a 2,8 GHz, perto dos 64 teóricos do AVX-512 com 2 FMAs).
2. **A banda de 94 GB/s era do cache L3** (260 MiB nesta máquina), não da DRAM. DRAM real: ~45 GB/s. Os tetos de tokens/s caíram pela metade.
3. **208 GFLOP/s era o melhor caso**, não a mediana. Nesta VM a mediana do campeão fica entre 130 e 165 GFLOP/s, com dispersão grande. Logo, a eficiência real é ≤ 20–25% do teto, não 70%.
4. "Passar no gate" foi descrito como prova. É evidência nível 2; só a **reprovação** é prova (contraexemplo).
5. Landauer foi apresentado como teto de J/FLOP. Não é.

## Os muros (versão precisa)

1. **Rice.** Não existe verificador geral de semântica. Daí a escada de evidência, em vez de fingir prova universal.
2. **Gödel II.** Um sistema não prova a própria consistência. O verificador pode melhorar numa cadeia
   (raiz → verificador 1 → verificador 2), desde que cada passo seja checado por algo que já é confiável.
   Um verificador que só afirma ser correto vale zero. A cadeia pode ficar mais rápida e melhor, mas nunca
   logicamente mais forte que a raiz.
3. **Roofline.** Gerar tokens um por vez está limitado por banda ÷ bytes dos pesos.
4. **Distribuição** não é impossível: é economia. `T = cálculo + comunicação + sincronização`; a MLG mede e recusa as ruins.
5. **No Free Lunch.** Nenhum otimizador é o melhor em média sobre todos os problemas. A MLG aprende
   P(estratégia | estrutura), não "o melhor otimizador".
6. **Landauer.** kT·ln2 por bit apagado irreversivelmente. É uma referência, não um limite de J/FLOP.

## Próximo muro

Os três vereditos dizem a mesma coisa: **o ganho está na implementação**, a ≤ 16–39% da parede de
cálculo. As suspeitas medíveis são criar threads em toda chamada (está dentro do tempo), não reorganizar
os dados em memória antes do cálculo (packing) e `expf` escalar na atenção. Cada uma entra como
transformação nova no espaço de busca, não como kernel escrito à mão.

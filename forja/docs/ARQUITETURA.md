# Forja — arquitetura

Jogo de ação 3D multiplayer em que o jogador **monta** os próprios poderes num editor de partida, ensaia
combos e duela usando exatamente o que criou. Este documento cobre as decisões caras de mudar, a linguagem
de criação, os custos, a rede e o caminho para a Unreal Engine 5.8.4.

## 1. Decisões caras de mudar (e o teste que as escolheu)

**Teste de aceitação usado para escolher a arquitetura:** o percurso completo do prompt precisa rodar com
servidor e jogadores em processos separados, usando só os comandos que a interface envia. Nenhuma habilidade
pode estar no código. O teste falha se houver atalho: `grep` por "Passo/Pulso/Travessia/Contramaré/Anteparo"
em `core/` e `server/` não encontra nada. Essas palavras só existem nos testes e nos dados do jogador.

| Decisão | Escolha | Ganho | Custo |
|---|---|---|---|
| Onde mora a lógica | Núcleo C++20 independente de engine (`core/forja`), sem exceções nem RTTI, `-Wshadow -Werror` | Roda aqui (testável), no servidor dedicado e dentro da UE como módulo. Uma única semântica. | Camada de adaptação na UE (posições, RPC, cues) |
| Formato salvo | Grafo tipado de operações, subcriações por digest sha256 de revisões imutáveis | Composição recursiva sem último nível, versões reproduzíveis | Editor precisa de comandos de grafo (ligar portas, dados) |
| Formato executado | Plano achatado, com escopos aninhados e limites estáticos | Interpretação barata e determinística no servidor | Recompilar ao trocar regras da sala |
| Autoridade | Servidor decide acertos, recursos, janelas, placar e fim | Justiça e reprodutibilidade | Clientes só preveem cosmético e deslocamento próprio |
| Oficina | Comandos JSON do núcleo (`Workshop::exec`) | A interface e os testes usam o mesmo caminho; desfazer e refazer valem para os dois | No protótipo web, o acervo fica no servidor; na UE, fica no cliente |
| Aparência | Fora do digest (cor, matéria, genoma do personagem) | Variação infinita sem mexer em regras ou competitividade | Precisa ser replicada à parte (anexada ao `exec.inicio`) |

## 2. Linguagem de criação: árvore, grafo ou fluxo de eventos (com a Contramaré)

* **Árvore** — `Seq[Marca(saida), Travessia, Janela(300, ao_comando: Volta(saida))]`. Legível, mas o dado que
  liga a marca à volta vira variável por nome, e sobreposição exige nós `Paralelo` artificiais.
* **Fluxo de eventos** — "ao ativar: marcar; quando Travessia terminar: abrir janela; se comando: voltar". É o
  mais natural para regras em linguagem de jogo, mas é difícil provar terminação, limite de eventos e ordem de
  eventos simultâneos quando as regras se cruzam.
* **Grafo tipado (adotado)** — nós são operações com portas de **fluxo** (eventos: `ao_chegar`, `ao_tocar`,
  `quando o anel terminar`, `se tocar água`…) e portas de **dados** tipadas (`ponto`). A Contramaré tem uma
  aresta de dados explícita `Marca.ponto → Volta.destino`. As regras em linguagem de jogo são *geradas* do
  grafo, e o validador prova que ele é acíclico, que os tipos casam, que o dado tem origem e que tudo termina.

### Formato salvo (comportamento: entra no digest)
```json
{"formato":"forja.receita/1",
 "nos":[{"id":"n0","op":"entrada.ativar","v":1,"p":{}},
        {"id":"n1","op":"marca.guardar","v":1,"p":{"ancora":"autor"}},
        {"id":"n2","op":"sub.incorporar","v":1,"p":{"ref":"sha256:…Travessia r1…","sobrepor":{}}},
        {"id":"n3","op":"cadencia.janela","v":1,"p":{"abre_ms":0,"dura_ms":300,"gatilho":"pressionar",…}},
        {"id":"n4","op":"vetor.retornar","v":1,"p":{"duracao_ms":120,"alcance_max_cm":1000,…}}],
 "fluxo":[{"de":"n0","porta":"ao_ativar","para":"n1"},{"de":"n1","porta":"depois","para":"n2"},
          {"de":"n2","porta":"ao_terminar","para":"n3"},{"de":"n3","porta":"ao_comando","para":"n4"}],
 "dados":[{"de":"n1","porta":"ponto","para":"n4","entrada":"destino"}],
 "expor":[]}
```
O exemplo completo e real (pacote com o fecho transitivo) está em `docs/exemplo_receita.json`. A execução
passo a passo no tempo do jogo está em `docs/exemplo_execucao.json`: 20 eventos, de `exec.inicio` (0 ms) até
`exec.fim` (720 ms), com a janela aberta em 450 ms e decidida pelo comando em 600 ms.

**Forma canônica e digest:** parâmetros normalizados (padrões preenchidos), nós ordenados por id, arestas
ordenadas e JSON sem espaços com chaves ordenadas. O digest é `sha256` disso e cobre:
- a versão de cada operação (`"v"` em cada nó);
- as dependências transitivas, porque cada `ref` é o digest de uma revisão imutável que, por sua vez, cobre as
  dela (Merkle).

Nome, rótulos e aparência ficam no registro da revisão, fora do digest.

### Formato executado (`Plan`)
Vetor de nós com parâmetros resolvidos, subcriações incorporadas em **escopos** aninhados (cada `sub.incorporar`
abre um escopo e segue por `ao_terminar` quando o escopo esvazia) e arestas resolvidas para índices. O plano
também guarda os limites estáticos: duração máxima, ativações, eventos, pico de trabalho, áreas e memória. Ele é
compilado no servidor a partir do pacote publicado, depois que os digests são recalculados.

### Unidades, espaços e ordem
* Centímetros, milissegundos inteiros e graus. Os eixos seguem a Unreal: X à frente, Y à direita, Z para cima.
* Espaços de referência:
  - `autor` (posição e frente no instante do nó);
  - `ativacao` (congelado no início);
  - `mira` (yaw enviado na ativação);
  - `evento` (ponto que veio com o token: alvo tocado, ponto de travessia);
  - `marca` (dado).
* Eventos simultâneos são ordenados por `(t, fase, execução, sequência)`. As fases são: 0 entrada, 1 estado
  (marcas, deslocamentos, anteparos), 2 contato e 3 fim/decisões. **Empate defesa × contato no mesmo ms: a
  defesa vence** (há um teste para isso).
* Ramificação vem das portas condicionais (`se_sim/se_nao`, `ao_tocar`, `ao_comando/ao_expirar`).
* Repetição só existe via `cadencia.repetir`, com no máximo 8 vezes e intervalo mínimo de 50 ms. Ciclos de
  fluxo são recusados (`ciclo_instantaneo`).
* Cancelamento:
  - pelo jogador, por desconexão, pelo próprio nó `cadencia.encerrar` ou pelo fim de ponto;
  - o que já entrou em contato resolve e o resto é removido, com os recursos devolvidos;
  - o prazo de segurança é `duração máxima + 2·tolerância + 100 ms`.
* **Toda execução termina:** o grafo é um DAG, a repetição é limitada, as durações são limitadas e há o prazo
  de segurança.

### Acrescentar uma operação e migrar versões
1. Entrada em `core/forja/ops.cpp` (nome, versão, fundamento, portas, parâmetros com faixa).
2. Caso em `fill_params` e na análise (`dur_range`, `port`, `phases_of`, custo) em `compile.cpp`.
3. Caso no interpretador (`on_token`/`on_timer`) em `runtime.cpp`.
4. Uma versão nova de uma operação existente mantém a antiga registrada como `obsoleta` com um adaptador em
   `adapt_to_latest`.
   - A receita antiga continua executando com o mesmo digest.
   - O comando `migrar` cria uma revisão nova e preserva a original.
   - O teste usa `limiar.anel@0`, que media por diâmetro.
5. Operações **planejadas** (`vetor.orbitar`, `limiar.projetil`, `lastro.zona`, `forma.mudar`…) já são
   nomeadas: a receita que as usa é preservada e o erro diz exatamente qual peça falta.

## 3. Fundamentos (classes originais) e operações disponíveis

| Fundamento | Papel | Operações v1 |
|---|---|---|
| **Vetor** | trajetórias e posições | `vetor.deslocar` (vetor, curva, referencial), `vetor.retornar` (até um ponto), `marca.guardar` |
| **Limiar** | travessias e contatos | `limiar.anel` (disco/anel, preparo, contato, impacto), `limiar.sondar` (superfície), `limiar.vigiar` (cruzar água/metal) |
| **Lastro** | sustentar e absorver | `lastro.guarda` (abertura, gesto, cargas), `lastro.amparo` (suporte a aliados) |
| **Cadência** | janelas e ritmo | `cadencia.janela`, `esperar`, `repetir`, `reunir`, `encerrar` |
| Subcriação | composição | `sub.incorporar` (revisão por digest, parâmetros expostos sobrepostos) |

Uma interação entre classes dentro de subcriação é a Contramaré: Vetor (marca e volta), Cadência (janela) e a
Travessia incorporada (Vetor + Limiar).

## 4. Custos

**Computacional** (por execução e por sala; bloqueia a execução):
- trabalho por tick: uma área custa `4 + células de 1 m²`, um deslocamento custa 2, uma janela custa 1;
- picos simultâneos são calculados por varredura de envelopes `[início mín, fim máx)` × multiplicidade;
- também entram ativações, eventos, memória, armazenamento (bytes canônicos com dependências) e tráfego
  (bytes por ativação numa codificação binária planejada, cerca de 4× isso em JSON).
- Limites: 64 un./tick, 4 áreas, 128 ativações e 512 eventos por execução; 512 un./tick, 24 áreas e
  32 execuções por sala. A admissão reserva o pico estático da execução.

**Exemplo numérico (testado).** Uma área de 3 m custa 4 + ⌈π·3²⌉ = 33 unidades.
- Em sequência, o pico é 33 ≤ 64: cabe.
- Em paralelo, o pico é 66 > 64: é recusado.

O editor aponta as duas etapas responsáveis e propõe duas ideias editáveis:
1. "encadear a segunda quando a primeira terminar";
2. "raio de até 2,98 m para caber em paralelo".

Nenhuma entra sem aprovação, e o jogador pode editar o valor antes de aprovar.

**Competitivo** (pontos; vale para a receita completa, com as subcriações):
- **Área:** 4·impacto + 3·área(m²) + contato/100 + alcance(m) − 0,5·preparo/100.
- **Mobilidade:** 2·distância(m) + bônus de velocidade.
- **Proteção:** abertura, cargas e duração, menos o preparo.
- **Janela:** +1,5.
- **Recuperação:** desconto.
- **Critérios:** anúncio ≥ 150 ms antes de qualquer impacto; alcance ≤ 12 m; ≤ 40 pontos por criação;
  equipamento ≤ 100; abertura ≤ 180°; ≤ 2 cargas.
- **Recarga** = 300 + 60·pontos ms. **Fôlego** = pontos/2.
- Valores medidos:

  | Criação | Pontos |
  |---|---|
  | Passo | 8,0 |
  | Pulso | 14,4 |
  | Travessia | 25,4 |
  | Contramaré | 35,9 |

Geração ilimitada de recursos é recusada: quem concede mais fôlego do que consome não vira revisão.

## 5. Rede

* **Topologia testada:** servidor dedicado num processo próprio e dois clientes em processos separados, na mesma
  máquina (loopback). LAN e internet não foram testadas aqui.
* **Publicação:** o cliente envia `publicar {pacote}` com a receita e o fecho de dependências. O servidor
  recalcula cada digest (recusa adulteração), compila com as regras da sala e guarda em cache por digest. O
  `equipar` aponta o digest. Treino e duelo usam os mesmos digests (verificado).
* **Ativação:** `in.botao {seq, slot, tipo, t_est, mira}`. O servidor descarta `seq` repetido (retransmissão).
  - Comandos dentro de uma execução são julgados **pela linha do tempo do próprio jogador**:
    `t0 + (t_cliente − t_cliente_ativação)`, limitado a `[agora − tolerância, agora]`.
  - A janela decide em `fecha + tolerância` (120 ms). Latência constante não altera o julgamento.
  - Teste: o comando chegou 800 ms após a ativação, com a janela fechando em 750, e foi aceito por ter sido
    feito aos 700 ms. Sem compensação, seria "tarde".
* **Eventos:** cada evento traz identificador de execução, tempo do servidor e dados suficientes para
  reconstruir luz, partículas e som. Janelas e comandos são privados do autor.
* **Estado:** quadros não confiáveis a 20 Hz com `ack` do último movimento. O cliente prevê o próprio andar e o
  primeiro deslocamento e concilia ao receber `mov.inicio`, ou desfaz se a ativação for recusada.
* **Condicionador por sentido:** latência de subida e descida, jitter, perda de subida e descida
  (mensagens não confiáveis são descartadas; confiáveis pagam retransmissão) e duplicação.
* **Entrada tardia:** espectador recebe snapshot com efeitos ativos.
* **Desconexão com efeito ativo:** áreas já em contato resolvem e o resto é cancelado; o duelo entra em pausa
  de 15 s; reconexão por token; abandono se o tempo esgotar.
* **Revanche:** os dois pedem; mesmos equipamentos, lados trocados.
* **Latência assimétrica** desloca a estimativa de relógio em (subida − descida)/2. Medido: 80/40 ms → ~20 ms
  de viés; a mediana observada pelo cliente foi 65 ms.

## 6. Unreal Engine 5.8.4 — onde termina o GAS e começa a linguagem do jogador

Versão fixada: **5.8.4** (última correção da 5.8 publicada no fórum oficial; a 5.8 é a última grande versão da
série 5). O projeto em `unreal/ForjaArena` traz o núcleo como módulo `ForjaCore` (compila as mesmas fontes) e
o módulo de jogo `ForjaArena`.

* **GAS:** atributos (`Folego`), custo e recarga (`GameplayEffect`), tags de estado (`Estado.Recuperacao`,
  `Estado.Deslocamento`), ativação e previsão de entrada. Uma única `UForjaAbilityReceita` cobre *todas* as
  criações: ela valida custo e recarga e entrega a ativação ao interpretador.
* **Linguagem do jogador (núcleo):** o que acontece depois da ativação (deslocamentos, áreas, janelas, defesas,
  subcriações) é o mesmo `forja::Runtime` do servidor dedicado. As combinações reutilizam o mesmo mecanismo.
  O GAS não conhece Passo nem Pulso.
* **Cues:** eventos do runtime viram `GameplayCue`/Niagara no cliente. A tabela de cues é um DataAsset; a
  matéria (cor, viscosidade, elasticidade, toxicidade, temperatura, eletricidade, fumaça) vira parâmetros de
  usuário Niagara.

### Direção visual na UE (justificativa)
* **Personagens realistas e variações infinitas:** MetaHuman (integrado à engine desde a 5.6) com o
  **genoma** do protótipo mapeado para parâmetros do MetaHuman Creator (corpo, rosto, pele, cabelo) e
  **Mutable** (Customizable Objects) para roupas e combinações em tempo de execução.
* **Corpos elásticos** via Control Rig (escala de ossos com mola, igual ao protótipo) e ML Deformer para
  deformação de qualidade.
* **Animação:** Motion Matching para locomoção, Control Rig para IK de pés e mãos (as mesmas camadas do
  protótipo) e montages derivadas dos marcos da receita (preparo, contato, recuperação).
* **Nanite** para o concreto, as juntas e os detritos da galeria (geometria densa, sem LOD manual).
* **Lumen** para a claraboia e o reflexo na água; ver o perfil "Lumen Lite" da 5.8 no nível gráfico médio.
* **Niagara** para todas as camadas de matéria; os sinais públicos ficam em materiais de decal com cor e
  forma fixas.
* **Dois níveis gráficos:** Épico (Lumen HW, sombras virtuais, Niagara completo) e Médio (Lumen SW, menos
  partículas). Os volumes, as regras e os sinais públicos são idênticos nos dois.

## 7. Protótipo web (o que roda agora)

* `client/` — Three.js r170 (MIT, único asset externo; tudo o mais é procedural).
* Oficina com regras em linguagem de jogo (condição editável), gizmos na cena, linha do tempo com passo de um
  tick, custos, sugestões aprováveis, revisões, comparação, atualização e reversão de dependências e editor de
  matéria.
* Arena da galeria de drenagem.
* Estúdio de animação (`/estudio.html`).
* **Humanos procedurais:** esqueleto com IK de pernas (pés plantados) e braços, pele contínua por anéis
  anatômicos, cabeça esculpida, olhos, cabelo e roupas. O genoma com semente dá variações infinitas, e o mesmo
  nome gera o mesmo personagem em todos os clientes.
* **Matéria:** eixos contínuos misturam camadas: gosma e poça corrosiva, chamas, cristais de gelo, arcos
  elétricos, fumaça e membrana elástica. Projéteis: gota, bola de fogo, lança de gelo, raio, feixe ou braço
  elástico.
* **Efeitos e animações** são funções puras de (evento, tempo do servidor): a prévia, as duas câmeras e a
  repetição mostram o mesmo quadro.

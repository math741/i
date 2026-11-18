# Forja — poderes criados pelo jogador

Jogo de ação 3D multiplayer: você monta o poder na oficina, ensaia o combo na linha do tempo e duela com
exatamente o que criou. O servidor decide tudo (acertos, janelas, defesas, placar), e o adversário lê cada ação
por sinais públicos.

* **Núcleo** (`core/forja`, C++20 sem exceções nem RTTI): linguagem de receitas em grafo tipado, digest Merkle
  de revisões, validador, compilador para plano, interpretador autoritativo, custos computacional e competitivo,
  oficina (desfazer/refazer, revisões, comparar, atualizar/reverter dependências, exportar/importar, migrar)
  e regras de sala.
* **Servidor dedicado** (`server/main.cpp`): HTTP + WebSocket, salas privadas, treino, duelo, placar, revanche,
  pausa por desconexão, condicionador de latência e perda por sentido.
* **Cliente web jogável** (`client/`): oficina com linha do tempo e gizmos, arena da galeria de drenagem,
  humanos procedurais com genoma (variações infinitas) e sistema de matéria para os efeitos (veneno, elástico,
  brasa, gelo, tempestade, fumaça, mercúrio e qualquer mistura).
* **Unreal Engine 5.8.4** (`unreal/ForjaArena`): projeto com o núcleo como módulo e adaptadores (GAS, RPC,
  cues Niagara). **Não compilado nesta sessão**; ver `unreal/LEIAME.md`.

## Rodar agora (Linux/macOS)
```sh
cd forja
cmake -S . -B build -G Ninja && ninja -C build          # núcleo, testes e servidor
./build/forja_tests                                      # 95 verificações
./build/forja_server --porta 8787 --cliente client --dados run/saves --logs run/logs
```
Abra `http://localhost:8787/?nome=Ana` numa aba e `http://localhost:8787/?nome=Bruno&sala=CODIGO` noutra (ou
noutro computador da rede trocando `localhost` pelo IP; LAN e internet não foram testadas nesta sessão).
O código da sala aparece no painel da Ana depois de "Criar sala".

* Estúdio de animação: `http://localhost:8787/estudio.html` (clipes, matérias, personagem aleatório, câmera lenta).
* Controles: WASD andar · mouse girar (clique para travar) · 1–4 criações (soltar = "ao soltar") · botão
  direito = secundário · C cancelar · Tab câmera (jogador/oponente/cima) · F gráficos alto/baixo · O oficina.

No Windows, use WSL para o servidor (ou compile com MSVC: o núcleo já compila sem exceções e sem RTTI). O
navegador pode rodar no Windows.

## Testes
| Comando | O que prova | Último resultado |
|---|---|---|
| `./build/forja_tests` | autoria (Travessia × inversão), Contramaré (cedo/dentro/tarde/ausente), Guarda 90° (frente × flanco), duelo e revanche, surpresa com previsão, pico em paralelo × sequência, multiplicação de eventos, geração de recursos, limite da sala, cancelamento, saída durante efeito, receita inválida, operação ausente, versão incompatível, migração, revisões após reinício, determinismo, empate defesa × contato, motor externo | 95/95 |
| `node tests/e2e/percurso.mjs` | o percurso inteiro do prompt com servidor + 2 jogadores em **processos separados**, reinício do servidor, rede com latência/perda/duplicação por sentido | 50/50 (`evidencias/e2e_relatorio.json`) |
| `node tests/ui/percurso_ui.mjs` | o mesmo pela interface real (Chromium, duas páginas), capturas das duas câmeras em dois níveis gráficos | rodado antes da troca para humanos; a reexecução foi interrompida (ver `docs/SESSAO.md`) |
| `node tests/ui/estudio_captura.mjs '[[clipe,ms,cam,matéria,semente],…]'` | quadros determinísticos de personagem, animação e efeitos | `evidencias/estudio/` |

## Documentos
* `docs/ARQUITETURA.md` — decisões, linguagem, formatos salvo e executado, custos, rede, GAS, UE.
* `docs/exemplo_receita.json` / `docs/exemplo_execucao.json` — Contramaré real e sua execução no tempo.
* `docs/SESSAO.md` — registro da sessão (versão, comandos, último teste aprovado, próximo critério).
* `unreal/LEIAME.md` — passos de editor (menus, assets, valores, resultado esperado).

## Assets externos
| Asset | Procedência | Licença | Custo |
|---|---|---|---|
| three.js r170 (`client/vendor/three.module.js` + addons de pós-processamento) | npm `three@0.170.0` | MIT (`client/vendor/THREE_LICENSE.txt`) | grátis |
| Texturas, personagens, efeitos, sons | gerados proceduralmente no código | próprios | — |
| UE 5.8.4, MetaHuman, Mutable, Niagara (alvo) | Epic Games | EULA da Unreal Engine | grátis até o limite de receita da EULA |

# Registro da sessão

## Versão
* Branch `ccr-7b337f54-vlmm2m`. O núcleo, o servidor e o cliente estão em `forja/`.
* Unreal fixada em **5.8.4** (última correção da 5.8 no fórum oficial em nov/2026). O projeto da UE não foi
  compilado aqui.

## Comandos
```sh
cd forja
cmake -S . -B build -G Ninja && ninja -C build
./build/forja_tests                      # núcleo
node tests/e2e/percurso.mjs              # percurso completo, processos separados
node tests/ui/percurso_ui.mjs            # interface real (Chromium headless, SwiftShader), ~10 min aqui
node tests/ui/estudio_captura.mjs '[[5,840,null,"Veneno"],[12,820,null,"Elástico"]]'   # quadros do estúdio
node tools/exemplo.mjs                   # regenera docs/exemplo_receita.json e docs/exemplo_execucao.json
./build/forja_server --porta 8787 --cliente client --dados run/saves --logs run/logs
```

## Último teste aprovado
* `forja_tests`: **95/95**.
* `tests/e2e/percurso.mjs`: **50/50**. Inclui o reinício do servidor, a previsão da Esteira (erro de 0,0 cm) e a
  compensação de latência variável (comando chegou em 800 ms com a janela fechando em 750, julgado em 700 →
  aceito).
* `tests/ui/percurso_ui.mjs`:
  - **Aprovados (antes da troca para humanos):** sala, treino, gizmo de raio arrastado na cena (156 cm) e
    desfazer, prévia da Travessia (atingido) e da inversão (fora).
  - **Não concluída:** a reexecução completa com os humanos e a matéria foi interrompida a pedido do usuário.
* Estúdio: quadros determinísticos em `evidencias/estudio/` (folhas `folha_estudio.png`, `folha_materias.png`
  e `folha_humanos.png`).

## Medições de desempenho (não representativas)
O renderizador é **SwiftShader na CPU**, num Intel Xeon @ 2,10 GHz com 4 vCPUs e 16 GB, a 1280×720. Deu cerca
de 2 fps, o que **não diz nada** sobre o notebook de referência (Windows 11, 24 GB, GPU ainda não identificada).
É preciso medir com GPU real e declarar CPU, GPU, VRAM, resolução e nível gráfico.

## Limitações conhecidas
* **Humanos:** procedurais, com cara de manequim e sem foto-realismo. O realismo fotográfico é o caminho
  MetaHuman + Mutable na UE (`unreal/LEIAME.md`).
* **Genoma:** hoje vem da semente do nome do jogador, igual em todos os clientes. Um genoma editado à mão ainda
  não é replicado; falta o campo `forma` no `ola` e no estado da sala.
* **Cabelo:** sem física. As faixas de tecido verlet existem só no operador blindado.
* **Topologia:** só foi testado o loopback, com processos separados na mesma máquina. LAN e internet ficam a
  validar.
* **Modo 2v2 e arena livre:** implementados nas regras. O 2v2 não tem teste de ponta a ponta.

## Próximos critérios de aceite
1. Reexecutar `tests/ui/percurso_ui.mjs` com humanos e matéria e chegar a 0 falhas, incluindo as capturas das
   duas câmeras em dois níveis gráficos.
2. Replicar um genoma personalizado. Teste: Ana edita a altura e o cabelo, Bruno vê exatamente a mesma pessoa.
3. Na UE (Listen Server, 2 jogadores): Travessia com alvo a 90 cm produz `area.contato` igual ao do servidor
   dedicado.
4. Medir FPS no notebook de referência nos níveis Épico e Médio, com os mesmos sinais.
5. Partida em LAN com dois computadores, registrando latência por sentido.

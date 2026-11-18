# ForjaArena — projeto Unreal Engine 5.8.4

> **Status honesto:** estes arquivos **não foram compilados aqui**: o ambiente desta sessão é Linux, sem GPU e
> sem a Unreal. O núcleo que eles incluem (`forja/core`) foi compilado e testado com as mesmas restrições do
> UBT (C++20, sem exceções, sem RTTI, avisos de sombra como erro). A integração abaixo é a parte que ainda
> precisa rodar dentro da engine.

## Abrir e compilar (Windows 11)
1. Instale a **Unreal Engine 5.8.4** pelo Epic Games Launcher e o Visual Studio 2022 com "Game development
   with C++" (inclui MSVC e o Windows SDK).
2. Clique com o botão direito em `ForjaArena.uproject` → **Generate Visual Studio project files**.
3. Abra `ForjaArena.sln`, selecione **Development Editor / Win64** e compile (`Ctrl+Shift+B`).
   * Se o MSVC acusar avisos de conversão no `ForjaCore`, não mude o núcleo; anote o aviso e reporte (o núcleo
     tem de continuar idêntico ao testado).
4. Abra o projeto. O editor carrega os módulos `ForjaCore` e `ForjaArena`.

## Montagem no editor (fatia vertical)
| Passo | Menu / ação | Asset | Valores | Resultado esperado |
|---|---|---|---|---|
| Mapa | File → New Level → Empty Open World desligado (Basic) | `/Game/Forja/Mapas/Galeria` | Galeria de 2800 × 900 × 500 cm centrada na origem; canal de 140 cm de largura ao longo de X (Y ∈ [-70, 70]), rebaixado 25 cm; juntas metálicas em X = ±700 cm (20 cm); claraboia 300 × 240 cm no teto em X = 0 | As mesmas medidas da arena do núcleo (`Arena::galeria`) |
| Água | Place Actors → Water Body Custom (ou plano com material de água) | `M_Agua` (translúcido, normal animado, IOR 1,33) | Superfície em Z = -5 cm, dentro do canal | O jogo consulta "água" pelo núcleo; o visual só precisa coincidir |
| Personagem | Content → Blueprint Class → pai `ForjaPersonagem` | `BP_ForjaPersonagem` | Capsule half-height 90, radius 34; Max Walk Speed 450 | Mesma cápsula do núcleo (raio 34 cm, altura 180 cm) |
| Humanos realistas | Window → MetaHuman → criar personagem; Edit → Plugins → **Mutable** para roupas | `BP_ForjaPersonagem` → Construction Script lê `Genoma` (JSON) | altura→escala de corpo; massa/músculo→biotipo; pele/subtom; rosto (mandíbula, queixo, nariz, lábios, maçãs); cabelo; roupa (Customizable Object) | Mesmo genoma do protótipo web → mesmo personagem em todos os clientes |
| Modo de jogo | Project Settings → Maps & Modes | Default GameMode `ForjaModoJogo` (ou BP filho apontando para `BP_ForjaPersonagem`) | Game State `ForjaEstadoJogo` | Ao entrar, cada jogador recebe um assento no `UForjaSubsistema` |
| Cues | Content → Miscellaneous → Data Asset → `ForjaCues` | `DA_ForjaCues` | Mapear `area.anuncio`, `area.contato`, `guarda.inicio`, `guarda.absorve`, `guarda.fim`, `mov.inicio`, `impacto` para sistemas Niagara | Efeitos reconstruídos dos eventos; matéria entra por `User.visc`, `User.elast`, `User.tox`, `User.temp`, `User.elet`, `User.fumaca`, `User.Cor`, `User.Raio`, `User.Preparo` |
| Niagara | Content → FX → Niagara System (vazio) | `NS_Anuncio`, `NS_Contato`, `NS_Guarda`, `NS_Rastro` | Camadas: chama (temp>0), cristais (temp<0), gosma e poça (tox/visc), arcos (elet), fumaça, anéis elásticos — pesos pelos parâmetros de usuário, como em `client/js/materia.js` | Variações infinitas por parâmetro, sem mudar regras |
| Sinais públicos | Decal Material `M_SinalAnuncio` | contorno branco → vermelho no contato; setor da guarda; anel vermelho de recuperação | Forma e contraste fixos (a cor personalizada só tinge o preenchimento) | Leitura igual para o autor e o oponente nos dois níveis gráficos |
| Entrada | Enhanced Input: `IA_Espaco1..4`, `IA_Soltar`, `IA_Secundario`, `IA_Cancelar` | `IMC_Forja` | 1–4 Pressed → `Pressionar(Espaco)`; Released → `Pressionar(Espaco,"soltar")`; RMB → `"secundario"` | Mesmo protocolo de botões do protótipo |
| Animação | Motion Matching (locomoção) + Control Rig (IK de pés e mãos, braço elástico por escala de ossos com mola) | `ABP_Forja` | Montages por marco da receita: preparo (antecipação), contato (golpe com 70 ms de pausa de impacto), recuperação (mãos nos joelhos) | Mesmas camadas do protótipo (`client/js/personagem.js`) |
| Iluminação | Lumen (Epic) e Lumen Lite/SW (Médio); luz direcional estreita pela claraboia | — | Exposição manual; neblina exponencial baixa | Dois níveis com os mesmos sinais |

## Rede na UE
* Autoridade: `UForjaSubsistema` existe apenas onde não é `NM_Client` (servidor dedicado ou anfitrião).
* Clientes chamam `Server_Botao/Server_Publicar/Server_Equipar/Server_Sala`. Os eventos voltam por
  `AForjaEstadoJogo::Multicast_Eventos` (confiável); a sala por `EstadoSala` (replicada).
* Posições: o `CharacterMovement` manda fora de deslocamento (`Runtime::sync_external`). Durante um
  deslocamento da receita o runtime manda e o subsistema move o ator.
* Teste local: Play → Number of Players 2, Net Mode **Play As Listen Server** (anfitrião) ou **Play As Client**
  com servidor dedicado separado (`ForjaArenaServer`, que exige engine compilada do código-fonte).
* Latência e perda por sentido: `Net PktLag`, `Net PktLoss` e `NetEmulation.PktLagVariance` no console de cada
  processo (configurações independentes no cliente e no servidor = sentidos independentes).

## Próximo critério de aceite na engine
Executar com 2 jogadores (Play As Listen Server):
1. Publicar Passo, Pulso e Travessia exportados pelo protótipo (`docs/exemplo_receita.json` contém a Contramaré
   com o fecho completo).
2. Equipar e ativar a Travessia com um alvo a 90 cm.
3. O log de `AForjaEstadoJogo::AoEvento` precisa mostrar `area.contato` com centro na chegada e "atingido",
   idêntico ao `evidencias/e2e_relatorio.json`.

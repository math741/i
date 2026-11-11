# PiFone

Sistema para Raspberry Pi Zero 2 W que funciona sem tela: liga, conecta no fone
Bluetooth e toca as musicas do cartao. Tudo e controlado pelos botoes do fone.

A imagem pronta (`pifone.img.xz`) fica em **Releases** deste repositorio e e
montada automaticamente pelo GitHub Actions a cada mudanca nesta branch.

## Como usar

1. Grave `pifone.img.xz` no cartao com o Raspberry Pi Imager (opcao "Use custom"),
   sem aplicar personalizacoes.
2. Coloque o cartao no Pi e ligue. Espere uns 2 minutos e desligue.
   Nessa primeira vez ele cria a unidade `MUSICAS` com o espaco livre do cartao.
3. Coloque o cartao no computador e copie as musicas para a unidade `MUSICAS`.
4. Ligue o Pi com o fone em modo de pareamento por perto. Ele pareia sozinho
   (so na primeira vez) e comeca a tocar.

## Depois

- Botoes do fone: play/pause, proxima, anterior. Volume e no proprio fone.
- Trocar de fone: crie um arquivo chamado `NOVO-FONE` na unidade `MUSICAS`.
- Ordem aleatoria: mude `ALEATORIO=sim` no arquivo `pifone.txt` da unidade `bootfs`.
- Se algo nao funcionar: o arquivo `pifone-log.txt` na unidade `bootfs` mostra
  o que aconteceu na penultima vez que o Pi ligou.
- Manutencao com teclado e HDMI: usuario `pi`, senha `pifone`.

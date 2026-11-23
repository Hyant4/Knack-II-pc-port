# Knack II PC 0.18.4

## Português

- Corrige a resposta do NGS2 a pedidos de velocidade de reprodução. O relógio acompanha as amostras consumidas, permitindo que o jogo recupere a sincronização de áudio em transições.
- Corrige o reinício de vídeos em loop no AVPlayer. Se o vídeo não conseguir reiniciar, o player encerra a tentativa com erro em vez de repetir a leitura continuamente.
- Mantém a validação aberta e os perfis já suportados na 0.18.3.

Em uma sessão Windows de aproximadamente 61 minutos, o áudio continuou na passagem para o capítulo 8. O teste cobre essa transição. O jogo não acompanha os pacotes.

## English

- Fixes how NGS2 handles playback speed requests. The audio clock tracks consumed samples so the game can recover synchronization during transitions.
- Fixes looping video restart in AVPlayer. Failed restarts report an error and stop retrying.
- Keeps the open validation workflow and profiles supported in 0.18.3.

Audio continued into Chapter 8 during a roughly 61-minute Windows playtest. The test covers that transition. Game data is not included.

# Áudio no port Windows

Na versão 0.14, a saída SDL mantém ganho geral de **1,30×**. As falas da
categoria **Voice** do jogo recebem ganho adicional de **1,50×**. Assim, antes
de eventuais limites de pico, o ganho das falas é 1,95× em relação à saída sem
esses dois ajustes. Música e efeitos recebem somente o ganho geral.

## Separação das falas

O contrato `data/scp_def/func_sound.def` do dump suportado identifica as categorias
de `SetConfigVolume`/`GetConfigVolume`: 0 para efeitos, 1 para música e 2 para
Voice. O port usa essa categoria explícita. Não classifica falas por codec,
índice de voz ou canal central. Áudio previamente misturado dentro de um vídeo
não é separado artificialmente em diálogo e música.

No perfil CUSA08014 01.01, dois caminhos nativos calculam ganho a partir dessa
categoria. O loader multiplica somente a categoria 2 por 1,50 e ajusta o limite
do setter de grupo somente quando a chamada vem de um desses dois caminhos e
o grupo pertence à categoria 2. Demais categorias e chamadores preservam o
limite original. O volume configurado pelo jogo não é regravado.

O ajuste verifica identidade do jogo, assinaturas das instruções, layout de
frame do setter e destinos das chamadas antes de modificar qualquer ponto em
memória. Não altera o dump no disco. Se uma verificação falhar, mantém o caminho
original e registra `dialogue gain installed=0`; instalação bem-sucedida registra
`dialogue gain installed=1 category=2 multiplier=1.50 sites=3`.

## Continuidade e pausa

O mixer normal combina até 256 fontes no mesmo bloco. O bypass de matrizes
silenciosas continua ativo; uma população acima da capacidade explícita é
rejeitada antes de consumir PCM ou relógios. O experimento legado B2-F2A de
32 vagas não faz parte do perfil normal do launcher.

O ajuste de falas não modifica codecs, cadência SDL, comandos de pausa,
geração das vozes, consumo de filas ou o relógio de amostras. Ganho zero
continua zero, preservando silêncios e pausas definidos pelo jogo.

## Evidências e limites

Testes executam os próprios stubs nativos e verificam as oito categorias,
dois caminhos de cálculo, limites de grupo, chamadores alheios, flags, registradores,
silêncio e configuração intacta. Assinaturas foram conferidas na imagem preparada
do dump suportado. Regressões de continuidade, pausa/retomada e relógio também
passaram. O novo ganho de diálogo ainda não recebeu escuta independente em jogo.

A sessão manual anterior de 4min17s exercitou a correção de continuidade e o
ganho geral: pico bruto de 105 fontes, sem descarte de fontes com matriz de
ganho não zero. O usuário relatou funcionamento favorável. Essa sessão antecede
o aumento específico das falas. Lacunas na alimentação SDL das primeiras janelas
continuam sendo uma questão separada; não há certificação da campanha completa.

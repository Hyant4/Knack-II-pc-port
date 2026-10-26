# Knack II PC — V0.17.6


Adiciona a predefinição manual **GTX 1060 black texture (experimental)** em
**Configurações > Vídeo > Predefinições de compatibilidade**.

- Aplica `gpu_readbacks=2` (Preciso) e `eager_readback=0` (leitura antecipada desligada).
- Preserva resolução, FSR, controles e os demais ajustes. Salve as configurações e reinicie o jogo para comparar a mesma cena.
- Não detecta a GPU automaticamente. Para reverter, escolha os valores anteriores nas opções de leitura e salve novamente.
- Mantém o início por `Play Knack II.exe`, a validação obrigatória do dump e a preferência de pré-carregamento da V0.17.5.
- Launcher recompilado com identificação 0.17.6 e interface PT/EN/RU. Runtime nativo e `Play Knack II.exe` preservados byte a byte do pacote V0.17.5.

**Limite:** é uma configuração experimental para investigar o relato da issue #12. A correção das superfícies pretas na GTX 1060 ainda não foi confirmada. Não inclui patch de shader/driver nem ampliação dos perfis de dump suportados.

Baixe `Knack-II-Windows-x64-V0.17.6.zip`, extraia a pasta inteira e abra `Knack II.exe`.
O pacote não inclui jogo, firmware ou saves. A V0.17.5 permanece disponível para retorno.
Fontes correspondentes e SHA-256 acompanham a release. Validações desta publicação são sintéticas e de pacote; nenhum jogo real foi iniciado.

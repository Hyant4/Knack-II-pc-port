# Knack II — 0.17.7-rc1

Pré-release experimental 0.17.7-rc1. A correção das superfícies pretas não está confirmada.

Inclui seleção/persistência corrigida do preset GTX e suporte por hash exato à variante relatada do executável US 01.01. Região, identidade do título e saves permanecem americanos. Hash desconhecido continua recusado. Launcher, supervisor e runtime foram recompilados juntos. Não inclui protótipo AA nem suporte USA 01.00 de outros trabalhos locais.

Extraia a pasta inteira em local novo e abra **Test GTX 1060.cmd**. O perfil de teste fica em `%LOCALAPPDATA%\KnackII-PC-Issue12-Test`, separado da configuração/progresso habituais. O atalho não inicia o jogo automaticamente. Selecione o dump atualizado intacto, valide, aplique o preset, salve e faça a mesma comparação inicial com FSR/RCAS desligados. Não troque executáveis nem edite metadados.

Após o teste, envie captura, `01-game.elf.log` e `session.json`, removendo caminhos pessoais. Logs devem registrar perfil US, layout compartilhado, readbacks precisos e leitura antecipada desligada. Dados do jogo e saves não devem ser enviados.

As verificações locais foram sintéticas, estáticas e do pacote. Não houve gameplay ou teste na GTX 1060. O fixture não comprova todos os assets da instalação do relator.

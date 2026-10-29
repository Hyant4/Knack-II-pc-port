# Mensagens de erro e idioma do launcher

As mensagens de erro da interface seguem a seleção **PT / EN / RU**, independente
do idioma escolhido para o jogo. Isso inclui validação de dump, instalação de
PKG, configurações, saves, inicialização e encerramento de sessões. Erros de
inicialização anteriores ao carregamento do Qt usam o idioma salvo do launcher.

O catálogo `port/launcher/knack2/ui/i18n_errors.py` contém mensagens completas e
modelos com parâmetros. A camada de apresentação identifica o modelo completo,
traduz o texto e preserva os valores: hashes, CUSA, versões, caminhos, nomes de
arquivos, chaves de configuração, códigos e números. Mensagens internas aninhadas
são traduzidas apenas nos campos explicitamente destinados a motivos e causas.
Não há substituição de palavras dentro de nomes de arquivos ou outros dados.

Mensagens conhecidas do helper PKG também têm traduções. Um diagnóstico externo
desconhecido recebe um título e o rótulo de detalhes técnicos no idioma escolhido;
o diagnóstico original é mantido para investigação. Os arquivos de log e a cópia
dos logs continuam preservando as linhas técnicas originais.

Notificações, histórico de erros, resultado da validação e estado do instalador
mantêm o texto-fonte. Trocar o idioma atualiza o que já está na tela, sem precisar
validar o dump ou instalar o PKG novamente. A mudança de idioma não altera o
resultado da validação, seus critérios, o backup, arquivos do jogo ou saves.

Ao acrescentar um erro interno novo, inclua seu modelo e as traduções no catálogo.
Os testes `tests/test_knack2_error_i18n.py` conferem cobertura das exceções atuais
dos módulos do launcher, preservação dos parâmetros, mensagens aninhadas, troca
de idioma na interface e erros de inicialização sem Qt. As mensagens produzidas
por terceiros não precisam ser previstas integralmente no catálogo: o título
localizado mantém o contexto da falha e os detalhes originais ficam disponíveis.

# Integração local do PR #19

Esta árvore consolida as alterações locais sobre os seis commits originais de
[SkippySteve no PR #19](https://github.com/Hyant4/Knack-II-pc-port/pull/19), HEAD
`12f0f60bf7f8529c8bea9de1cfa8483832ee6c88`. Os commits e seus autores/coautores
foram preservados. As adaptações posteriores pertencem à consolidação local.

Os dois últimos commits do PR tornam firmware NGS2 explicitamente opcional
(`2f322d2`) e separam a saída padrão experimental (`12f0f60`). O caminho HLE
atual continua sendo o padrão em Windows e Linux.

## Opções e validação

- `python run.py --game-dir /seu/dump --firmware-ngs2` procura
  `libSceNgs2.sprx` fornecido localmente e falha se não o encontrar. Sem a opção,
  não procura, não vincula firmware e não herda `TLG_NGS2` do ambiente.
- `KnackRunner(..., firmware_ngs2=True)` passa a mesma opção ao supervisor.
  Nenhum firmware acompanha fontes, builds ou AppImage.
- `--experimental-build`, no supervisor/launcher, mantém perfil e diretório
  padrão separados. O perfil é US 01.01 e fixa o hash alternativo `0b7843d3…`.
- A árvore local anterior já aceitava esse hash exato como variante do US 01.01.
  Essa compatibilidade foi mantida, com seleção do layout `elf-0b7843d3` e
  checagem de suporte do runtime. Não foram ampliadas regiões ou versões.
- O modo normal continua selecionando US 01.00, US 01.01 e EU 01.01 pelo
  `TITLE_ID`/`APP_VER` do SFO, com hashes completos e módulos obrigatórios.
- `python -m port.launcher.knack2 --real --language en-US` seleciona inglês como
  idioma inicial. A validação mantém os idiomas limitados ao dump e a preferência
  explícita de idioma da interface continua independente do idioma do jogo.

O launcher preserva a instalação PKG, perfis regionais, diagnósticos GPU, Native
AA/resolução e supervisão atuais. O isolamento experimental não migra saves.
Traduções dos novos rótulos usam os catálogos existentes, inclusive russo;
logs externos/guest e valores técnicos permanecem íntegros.

## Builds e limites

O harness SSE4A Windows reserva um frame explícito em casos INT3, mantendo
checks de registradores, flags e canários. O Windows não garante preservação
da memória abaixo do RSP durante exceções; veja a
[documentação de pilha x64](https://learn.microsoft.com/en-us/cpp/build/stack-usage).
Isso adapta a fixture ao contrato do host; o rewriter de produção permanece igual.

Use os guias [Windows](DESENVOLVIMENTO_WINDOWS.md),
[Linux](DESENVOLVIMENTO_LINUX.md) e [AppImage](APPIMAGE_LINUX.md).
O guia [Linux do autor](LINUX.md) registra também seu ambiente Fedora/Bazzite.
Relatos do autor e smokes locais são evidências distintas.

Arte da interface e ícones obtidos localmente não integram o commit de fontes.
Os builds locais podem incorporá-los como recursos de apresentação. Jogo,
firmware, saves, credenciais e notas internas não são versionados.

A consolidação não promete correção de device loss, desempenho universal ou
campanha completa. A dispensa de teste na configuração pessoal foi mantida.
O AppImage feito na base Ubuntu 26.04 requer glibc 2.43+; seu formato não reduz
o requisito de glibc nem certifica compatibilidade com Mint.

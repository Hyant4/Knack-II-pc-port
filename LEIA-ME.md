# Knack II — 0.17.7-rc2

## Português

Pré-release experimental para investigar superfícies pretas na GTX 1060. **Ainda não é uma correção gráfica confirmada.**

O preset **GTX 1060 black texture** agora oferece diagnóstico opcional, desligado por padrão:

1. **Cache novo por sessão:** usa uma pasta gráfica vazia e separada, preservando o cache habitual.
2. **Cache novo + compilação síncrona:** acrescenta espera pela compilação de shaders gráficos e compute. Pode causar pausas.

Extraia todo o ZIP em uma pasta nova e abra **Test GTX 1060.cmd**, que usa um perfil de teste separado. Valide seu update original intacto. Em Configurações → Vídeo, aplique o preset GTX, escolha o diagnóstico e salve. Compare a mesma cena, primeiro com diagnóstico desligado, depois opção 1 e por último opção 2. Mantenha os outros ajustes iguais. Trocar de preset desativa o diagnóstico.

Modo e caminho do cache são registrados nos logs e em `session.json`. Compartilhe esse arquivo, `01-game.elf.log` e uma captura da cena, removendo caminhos pessoais. Não é necessário trocar ELF, editar SFO ou reenviar hashes.

Launcher e supervisor foram recompilados juntos; o runtime nativo é exatamente o da RC1 e mantém seu identificador interno `issue12-candidate-0.17.7`. A validação estrita da variante ELF continua ativa. Outros presets mantêm seu comportamento. Trabalhos locais paralelos de AA, instalador PKG e novas traduções não fazem parte deste pacote.

Validação: 79 testes aprovados, um skip por harness nativo opcional ausente e dois contratos antigos de diagnóstico GPU fora do gate. Pacote compilado testado com três modos e duas etapas de LoadExec usando um executável sintético que não carrega o jogo; preferências antigas e ícones associados conferidos. Não houve teste real na GTX 1060 ou execução de gameplay. Stable/latest **0.17.5** permanece preservada.

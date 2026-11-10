# Versões de referência e versões não verificadas

Hashes conhecidos identificam os executáveis de referência já avaliados. Um
hash diferente, isoladamente, não comprova corrupção ou incompatibilidade.

Para os títulos e versões com perfil existente, o launcher permite executáveis
desconhecidos quando o parser do carregador confirma uma estrutura ELF/SELF
suportada, os segmentos necessários estão disponíveis e a entrada está em um
segmento executável. Metadados, arquivos obrigatórios e módulos continuam sendo
verificados. Arquivos incompletos, estruturas não suportadas e títulos ou versões
sem perfil continuam bloqueados.

## Aviso ao usuário

Quando algum executável não corresponde a uma referência conhecida, a tela
inicial e o resultado da validação mostram:

> Versão não verificada: qualidade, compatibilidade e estabilidade não foram
> avaliadas para estes executáveis.

O botão muda para **Jogar mesmo assim**. O usuário pode iniciar manualmente.
Início direto ou automático não pula esse aviso, mesmo com validação anterior
salva. O aviso é traduzido em PT/EN/RU e os idiomas adicionais do launcher.

## Layout e diagnóstico

Hashes conhecidos de `game.elf` continuam selecionando o layout correspondente.
Se o hash for desconhecido, o perfil regional fornece o layout de referência
para a tentativa, explicitamente classificado como não verificado. Não se afirma
que o código seja idêntico. As verificações nativas de assinatura dos patches
permanecem e podem impedir recursos ou encerrar uma tentativa incompatível.

O supervisor usa a mesma validação do launcher. `session.json` registra:

- `verification_status`: `reference` ou `unverified`.
- `unverified_executables`: nomes dos executáveis desconhecidos.
- `executable_hashes`: hashes efetivamente encontrados.
- `executable_layout_verified`: se o layout foi identificado por hash conhecido.

O log também mostra aviso de versão não verificada. Reconhecer um hash de
referência não equivale a certificar campanha completa ou qualidade em todo hardware.

## Escopo da mudança

Política solicitada para permitir teste de variantes legítimas como a da issue
#26. Não muda ABI, preparação/linking, runtime nativo, GPU, áudio, perfis JSON ou
caminhos de saves. O caso do relator ainda exige teste prático com o candidato.

# Extração do PKG do jogo-base

Na aba **Instalação**, clique em **Extrair jogo-base (.pkg)** e selecione seu
PKG local do Knack II. Não é necessário selecionar uma instalação antes.

O destino é fixo: **a própria pasta onde o PKG selecionado está**. Não existe
seletor de outro destino e não é criada uma subpasta com o nome do jogo.
Por exemplo, um PKG em `D:\Jogos\Knack\base.pkg` produz `D:\Jogos\Knack\eboot.bin`,
`D:\Jogos\Knack\game.elf`, `D:\Jogos\Knack\data` e as demais pastas do dump.
O PKG original permanece nesse local.

Antes de iniciar, a confirmação mostra região, versão e destino. O launcher
aceita somente PKG-base com identidade suportada pelo port; updates e DLC
continuam usando outro fluxo ou sendo rejeitados. A base americana CUSA08014
01.00 é suportada. Europa 01.00 continua sem perfil autorizado.

Arquivos existentes não são sobrescritos nem mesclados. Se a pasta já contém
`data`, `sce_sys`, `sce_module`, `eboot.bin` ou `game.elf`, a operação é bloqueada.
Conflitos adicionais encontrados antes da publicação também bloqueiam a operação.

Uma pasta temporária `.knack-base-extraction` é criada dentro da pasta do PKG.
O extrator calcula o tamanho descompactado antes de escrever os arquivos PFS;
o volume precisa comportar PKG e arquivos extraídos ao mesmo tempo. Depois da
extração, o launcher verifica metadados, SHA-256 completos e módulos obrigatórios.
Somente então move arquivos e pastas para o destino final, por renomeação no
mesmo volume. Não cria uma segunda cópia do dump descompactado.

**Cancelar operação PKG** interrompe o processo e remove seus arquivos temporários.
Se a publicação já começou, os itens novos são recolhidos antes da limpeza.
O fechamento normal do launcher solicita cancelamento e aguarda a conclusão.
Um registro permite recuperar uma publicação interrompida no próximo uso;
se a identidade dos arquivos divergir, a pasta é preservada e o erro indica
onde investigar. Não remova manualmente arquivos enquanto a operação estiver ativa.

Ao terminar, a pasta do PKG é selecionada no launcher e passa por nova validação.
O jogo só pode iniciar após validação bem-sucedida. O fluxo não usa nem altera
os saves habituais. Mensagens e confirmação estão disponíveis em PT/EN/RU.

Validação da implementação: testes com PKGs sintéticos criptografados,
cancelamento, conflitos, falhas de publicação e recuperação. O PKG americano
real também passou pela análise de identidade, SHA-256 e tabela PFS, sem repetir
a extração completa de aproximadamente 52 GB nessa etapa.

Em teste posterior, o usuário relatou extração normal, gameplay funcionando e
bloqueio de update europeu sobre base americana. O teste automatizado regional
também verifica o sentido inverso: jogo europeu rejeita update americano antes
de iniciar o extrator e preserva os arquivos instalados. Esse segundo sentido
tem evidência sintética; não foi apresentado como teste manual com PKG real.

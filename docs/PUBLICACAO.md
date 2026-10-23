# Publicação e dados locais

O objetivo deste projeto é disponibilizar os fontes do port de Knack II para colaboração pública. Créditos, avisos de copyright e licenças das bases e dependências são preservados; consulte [CREDITS.md](CREDITS.md). O dump e os recursos do jogo devem ser obtidos localmente pelo proprietário e não acompanham os fontes.

## Arquivos que permanecem locais

As regras de `.gitignore` cobrem instruções de agentes (`AGENTS.md`, incluindo variações de caixa), handoffs internos na raiz, diretórios de ferramentas de agentes, variáveis de ambiente locais, chaves privadas, arquivos de credenciais e diretórios de saves, logs, capturas e firmware. Dump, imagens preparadas e saídas de build também permanecem ignorados.

Exemplos de configuração sem segredos (`.env.example` e `.env.sample`) e as fixtures binárias sintéticas explicitamente permitidas continuam disponíveis. Não remova licenças, pins ou fontes de dependências para reduzir o tamanho do repositório.

## Limites do ignore

O `.gitignore` impede a inclusão comum de arquivos não rastreados. Não remove arquivos já versionados, não apaga commits antigos e pode ser contornado por `git add -f`. A documentação oficial explica como [arquivos rastreados são tratados](https://git-scm.com/docs/gitignore).

`AGENTS.MD` e `HANDOFF_ASTRA.md` foram retirados da versão atual para manter instruções internas e notas operacionais locais. Cópias anteriores continuam no histórico e em branches antigas. Essa organização não declara esses documentos como credenciais nem afirma que o histórico foi apagado.

Se uma credencial real for publicada, revogue-a ou substitua-a no serviço correspondente. Retirar o arquivo da versão atual não invalida a credencial. Qualquer reescrita de histórico requer planejamento separado, backup e coordenação com os colaboradores.

## Visibilidade no GitHub

Atualizar os fontes e alterar a visibilidade são operações distintas. Este guia não afirma que o repositório já esteja público. Confira o estado na página do GitHub.

O proprietário pode usar **Settings → General → Danger Zone → Change repository visibility → Make public**, seguindo as confirmações apresentadas pelo GitHub. Consulte a [documentação oficial de visibilidade](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility).

A mudança expõe os branches e o histórico acessíveis, além dos logs de Actions disponíveis. Portanto, retirar documentos internos apenas de `main` não torna as versões anteriores privadas.

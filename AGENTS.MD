# AGENTS.md — Diretrizes de Escopo e Segurança (Port Knack 2 PC / CUSA08014)

## 1. Contexto da Arquitetura
Este repositório contém um port nativo de PC para **Knack 2 (CUSA08014)**, baseado na arquitetura de execução direta do binário x86-64 da Orbis OS (`eboot.bin`) acoplado a um runtime C/C++ customizado e ao backend gráfico Vulkan (derivado do shadPS4).
- O projeto encontra-se em desenvolvimento híbrido entre dois agentes.
- A camada de baixo nível (ABI, memória virtual, hooks de endereço fixo e sincronização de GPU) está sob responsabilidade exclusiva do **GPT-6 Astra** e encontra-se **CONGELADA** durante esta sessão.

---

## 2. ZONA BLOQUEADA (READ-ONLY — PROIBIDO EDITAR)
Você **NÃO DEVE** modificar, refatorar, reformatar (ex: `clang-format`) ou "limpar" nenhum dos arquivos ou diretórios abaixo. Você pode apenas **lê-los** para consultar assinaturas de funções, *structs* e *headers*:

- `src/runtime/runtime_*.c` e `src/runtime/runtime_*.cpp` (Núcleo de execução e bootstrap do `eboot.bin`)
- `src/memory/*` (Mapeamento de memória virtual, `mmap`/`VirtualAlloc`, gerenciamento de páginas e *heaps* unificados)
- `src/abi/*` ou arquivos contendo trampolins Assembly (`.s`, `.asm`) e wrappers `SYSV_ABI`
- `src/video_core/renderer_vulkan/*` (Pipeline Vulkan, *buffer cache*, *readbacks* de Sunstones e *shader patches* `71a72db2`, `789ec898`, `21a668eb`)
- `src/hooks/*` (Qualquer hook em endereço de memória fixo `0x...` ou *detour* de função interna da engine)

> **REGRA DE OURO:** Se durante a compilação ou execução você identificar um *bug*, *crash* ou *warning* originado nos arquivos da Zona Bloqueada, **NÃO tente consertá-lo alterando esses arquivos**. Apenas documente o diagnóstico no arquivo `HANDOFF_ASTRA.md`.

---

## 3. Regras Estritas de Código (Anti-Refatoração Proativa)
1. **Preserve Convenções de Chamada:** Nunca remova ou altere macros como `__attribute__((sysv_abi))`, `__attribute__((ms_abi))`, `volatile`, `#pragma pack` ou `alignas(...)`.
2. **Não mexa em Offsets e Tamanhos de Structs:** As *structs* que fazem interface com a Orbis OS (`Sce*`) possuem layout binário estrito. Não adicione, remova ou reordene campos.
3. **Sem Refatoração Cosmética:** Faça alterações cirúrgicas. Não renomeie variáveis globais, não converta ponteiros brutos em *smart pointers* em áreas de interface C/ABI e não altere arquivos fora do escopo da tarefa pedida.
4. **Verificação Git Antes de Finalizar:** Após qualquer tarefa no terminal, execute `git status` e `git diff --name-only` para garantir que nenhum arquivo da Zona Bloqueada foi tocado acidentalmente.

---

## 4. ZONA PERMITIDA (Escopo do Gemini 3.8 Flash)
Você tem autonomia total para criar, editar e testar código nos seguintes módulos:
- **Launcher e Configurações:** Criação/edição da interface de inicialização, parser de arquivos `.ini`/`.toml` e passagem de argumentos de linha de comando (ex: `-highResolutionEffect`, seleção de resolução, *fullscreen*, VSync).
- **Overlay e Debug UI:** Implementação de menus em **ImGui** (contador de FPS, telemetria de frame time, inspetor de flags de debug).
- **Módulos HLE Isolados (Stubs e Wrappers de Alto Nível):**
  - `scePad` (Mapeamento de controles SDL2/XInput para DualShock 4, vibração e barra de luz).
  - `sceSaveData` / `sceAppContent` (Redirecionamento seguro de caminhos de save e montagem de diretórios locais).
  - `sceUserService` / `sceNpTrophy` (Perfis locais de usuário e sistema de conquistas/troféus em JSON/SQLite).
  - `sceAudioOut` (Ajustes de backend de áudio SDL/Cubeb que não alterem as threads de sincronização do core).
- **Sistema de Build e Ferramentas:** Ajustes em `CMakeLists.txt` (apenas para adicionar novos arquivos da Zona Permitida), scripts de automação Python e ferramentas de análise/filtragem de logs do Vulkan Validation Layer.

---

## 5. Protocolo de Handoff (`HANDOFF_ASTRA.md`)
Ao final de cada tarefa concluída, atualize (ou crie) o arquivo `HANDOFF_ASTRA.md` na raiz do projeto contendo:
1. **Arquivos Criados/Modificados:** Lista exata do que foi alterado.
2. **Novas Interfaces/Headers:** Assinaturas de funções que você criou e que precisarão ser conectadas ao runtime principal depois.
3. **Anomalias Detectadas (Read-Only):** Qualquer *crash*, *segfault*, *validation error* ou endereço de memória suspeito observado nos testes que exija intervenção do GPT-6 Astra na Zona Bloqueada.
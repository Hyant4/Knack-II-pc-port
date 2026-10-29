# HLE-2.2A0-FIX — barreira global da LibAtrac9

Data: 18/10/2026

Executor: GPT-5.6 Sol

Branch/HEAD inicial: `knack2-windows` / `6856cce`

## Sintoma

O stress `test_runtime_atrac9_lock.c` falhava intermitentemente em `!violation`. O marcador é
ativado somente quando os mocks de `Atrac9InitDecoder()` e `Atrac9Decode()` observam execução
simultânea. A falha foi reproduzida novamente na iteração 6 de uma bateria planejada de 50
processos.

## Causa demonstrada

O falso positivo da instrumentação foi descartado com um probe que usa diretamente a mesma
implementação de `pthread_rwlock` do MSYS2 CLANG64, sem o adaptador ou a LibAtrac9. Com
`PTHREAD_RWLOCK_INITIALIZER`, `pthread_rwlock_rdlock()` retornou `EINVAL` (`22`) sob contenção.
O adaptador não verificava esse retorno e chamava `Atrac9Decode()` como se o read lock tivesse
sido adquirido. A instrumentação detectava corretamente a sobreposição resultante.

O mesmo probe, alterado apenas para executar `pthread_rwlock_init()` antes de publicar o lock,
passou em 100 processos consecutivos. Portanto, a causa é a inicialização estática/lazy do
rwlock nessa combinação de Winpthreads/toolchain, não uma barreira duplicada por handle.

Busca de call sites confirmou uma única barreira em `runtime_atrac9.c`. AJM e NGS2 chamam
`runtime_atrac9_init()` e `runtime_atrac9_decode()`; não há chamada direta à LibAtrac9 nos
arquivos de produção dessas rotas.

## Correção

`runtime_atrac9.c` agora:

- inicializa o rwlock global uma vez com `pthread_once()` + `pthread_rwlock_init()`;
- não expõe nenhum handle se a inicialização do gate falhar;
- trata falha de `pthread_rwlock_wrlock()` ou `pthread_rwlock_rdlock()` como erro fechado, sem
  chamar a LibAtrac9 fora da barreira;
- mantém o mutex por decoder, permitindo Decode concorrente entre handles distintos depois da
  inicialização.

Nenhuma ABI pública, configuração do codec ou representação PCM mudou.

## Evidências após a correção

- Stress focado do adaptador: 100/100 processos, 30.000 operações por processo, total de
  3.000.000 operações; nenhuma violação. O teste também exige dois Decodes concorrentes em
  handles independentes.
- Probe com inicialização explícita do rwlock: 100/100 processos.
- Testes focados de adaptador real, AJM, HLE-1 e HLE-2.2A1: 6/6.
- PCM de referência preservado: hash `a79abed856a64db5`.
- Suíte `tests/`: 29/29, incluindo o probe explícito do rwlock.
- `build.ps1 -Jobs 6`: aprovado; somente avisos CMake preexistentes.
- `git diff --check`: executar novamente na entrega final.

HLE-2.2A1 permanece bloqueado até revisão específica dos resultados. HLE-2.2B não foi iniciado.

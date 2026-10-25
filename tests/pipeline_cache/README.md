# Cache regression probes / Testes de regressão do cache

After configuring the native CLANG64 build in `port/out/release` with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, run from this source root using the existing Python/toolchain:

```text
python tests/pipeline_cache/run_probe.py synthetic
python tests/pipeline_cache/probe_startup.py startup
```

The first probe uses production loader/deserializer/comparator code with host-service test doubles. The second checks on-demand startup. Together they run 22 synthetic checks without launching the game or using game data. These are not GPU or gameplay tests. Compile tools are expected in the existing `LOCALAPPDATA/knack2-toolchain/msys64/clang64/bin` directory.

Após configurar a build nativa CLANG64 nesse diretório, execute os comandos acima na raiz dos fontes. Os dois probes totalizam 22 verificações sintéticas, sem executar jogo ou usar dados proprietários. Serviços do host são simulados; isso não substitui testes de GPU/gameplay.

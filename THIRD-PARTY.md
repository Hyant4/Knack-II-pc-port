# Bibliotecas e fontes de terceiros

Os fontes e avisos do projeto acompanham o snapshot desta release. O conjunto
inclui componentes GPL e LGPL, além de licenças permissivas. Consulte os textos
em licenses/, os avisos dos próprios fontes e docs/CREDITS.md. Os direitos de
modificar, substituir e reconstruir componentes permanecem conforme suas licenças.

O launcher usa Qt/PySide6 6.12.0 e Shiboken6 (LGPLv3/GPL), CPython 3.14.5,
iced-x86 1.21.0 e o bootloader PyInstaller 6.22.3 com sua exceção de distribuição.
Fontes oficiais dessas versões:
- https://code.qt.io/cgit/pyside/pyside-setup.git/tag/?h=v6.12.0
- https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/
- https://www.python.org/ftp/python/3.14.5/Python-3.14.5.tar.xz
- https://github.com/icedland/iced/tree/v1.21.0
- https://github.com/pyinstaller/pyinstaller/tree/v6.22.3

As DLLs do runtime são copiadas dos pacotes MSYS2 CLANG64 identificados, com
versões, hashes e origens, em DEPENDENCIES.json. Receitas e fontes de pacotes:
https://github.com/msys2/MINGW-packages e https://repo.msys2.org/mingw/sources/.
O FFmpeg utilizado declara GPLv3 ou posterior; seus avisos não são substituídos
pelo texto GPLv2 preservado do projeto. Drivers Vulkan não acompanham o pacote.


FSR 4 v07 INT8 experimental: modelos/shaders Q2RTX na revisão `ae8d628fae208813172446d1e49ed94150b04658`. Licença e proveniência em `port/fsr4_shaders/LICENSE-FSR4-v07.txt` e `FSR4-ASSETS.json`. Variantes otimizadas usam os scripts existentes do projeto; não são FSR 4.1.1.


Windows FSR 4 v07 shader update: original pass-11 SPIR-V with verified bounds guard; original postpass preserved. Preparation uses SPIRV Tools. Source/licence remains Q2RTX at the revision listed in `FSR4-ASSETS.json`.


Windows FSR 4 v07 shader update: original pass-11 SPIR-V with verified bounds guard; original postpass preserved. Preparation uses SPIRV Tools. Source/licence remains Q2RTX at the revision listed in `FSR4-ASSETS.json`.

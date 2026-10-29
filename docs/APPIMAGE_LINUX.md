# AppImage Linux — V0.18

O AppImage inclui Python, Qt e bibliotecas nativas de áudio, vídeo e interface.
A glibc e os drivers gráficos permanecem no sistema. Resultados do candidato
atual ficam no BUILDINFO.json; relatórios antigos descrevem somente suas
respectivas compilações, não o estado da V0.18.

Este candidato foi compilado no Ubuntu 26.04 e requer **glibc 2.43 ou superior**,
Linux x86-64 e driver com Vulkan 1.3. Não há validação de compatibilidade com
Linux Mint. Para atender distribuições com glibc anterior, será necessário
recompilar o runtime e suas dependências em uma base mais antiga; mudar apenas
o formato de distribuição não reduz o requisito de glibc.

## Executar

```bash
chmod +x Knack-II-x86_64-0.18.0.AppImage
./Knack-II-x86_64-0.18.0.AppImage
```

Se FUSE estiver indisponível, use o modo de extração do runtime AppImage:

```bash
./Knack-II-x86_64-0.18.0.AppImage --appimage-extract-and-run
```

A abertura mostra o launcher e não inicia o jogo automaticamente. Selecione um
dump próprio já extraído. O extrator PKG nativo Linux não acompanha este build.
O jogo, saves e firmware não acompanham o AppImage. Preferências e dados de
usuário continuam fora da imagem, no diretório de dados do usuário.

## Reproduzir o empacotamento

Use o ambiente Linux de empacotamento com PyInstaller já disponível. Primeiro
gere um bundle com `scripts/build_knack_linux.py` e confira seu manifesto.
Disponibilize localmente as ferramentas oficiais
[appimagetool](https://github.com/AppImage/appimagetool) e
[type2-runtime](https://github.com/AppImage/type2-runtime), com seus hashes
verificados. Extraia o appimagetool com `--appimage-extract` em uma pasta isolada.

```bash
python scripts/build_knack_appimage.py \
  --bundle /pasta/Knack-II-Linux-x86_64-dev \
  --build-dir /pasta/nova/appimage-build \
  --appimagetool /pasta/ferramentas/squashfs-root/AppRun \
  --runtime-file /pasta/ferramentas/runtime-x86_64 \
  --runtime-license /pasta/ferramentas/runtime-LICENSE.txt \
  --version 0.18.0
```

O script não baixa dependências, exige saída nova, confere o manifesto de
entrada e cria AppDir, avisos de licença, manifesto final, relatório de
dependências e AppImage Type 2. Não troca drivers, não instala arquivos no
sistema e não executa o jogo. O build requer `ldd`, `readelf`, `dpkg-query` e
as ferramentas de compactação usadas pelo appimagetool.

O formato segue a [estrutura AppDir](https://docs.appimage.org/reference/appdir.html)
e o [empacotamento manual oficial](https://docs.appimage.org/packaging-guide/manual.html).

# Knack II PC — V0.17.2

Atualização do launcher com alternância PT / EN / RU. A interface muda na hora e guarda sua preferência. O idioma do jogo continua sendo configurado separadamente em Configurações.

## Instalação

Baixe `Knack-II-Windows-x64-V0.17.2.zip`, extraia a pasta inteira e abra `Knack II.exe`. Valide seu próprio dump CUSA08014 ou CUSA07670, versão 01.01. Preferências e saves ficam em `%LOCALAPPDATA%\KnackII-PC`.

## Conteúdo

- Launcher Qt/QML em português, inglês e russo.
- Runtime preservado byte por byte da V0.17.1; jogo e renderer não mudam nesta atualização.
- Salve a preferência da interface. Idioma do jogo continua independente.
- Fontes correspondentes incluídas em `Knack-II-sources-V0.17.2.zip`.

## Validação e limites

- Compilação PyInstaller 6.22.2 e PySide6-Essentials 6.12.0.
- Testes focados do launcher e idiomas: 48 aprovados.
- Smokes do pacote: demonstração QML, launcher real em idle, encerramento controlado de processo sintético, ajuda do supervisor, ajuda ISA, CPU e Vulkan. Nenhum jogo iniciado.
- Campanha, áudio audível e desempenho em partidas reais não foram reavaliados nesta atualização. A V0.17.1 permanece disponível como rollback.

## Windows 0.18.8.1

Corrige a falha `verification_status` que interrompia o supervisor antes de abrir o jogo. O ZIP da 0.18.8 incluía uma cópia antiga de `run.py`. Este pacote inclui o script correspondente ao supervisor e ao launcher.

Baixe e extraia o ZIP inteiro em uma pasta nova. O launcher continua usando os dados de usuário da instalação anterior.

A abertura do launcher e a preparação dos dois executáveis foram verificadas no Windows com um dump americano. A edição chinesa CUSA08096 01.01 ainda aguarda reteste de boot, legendas e voz na [issue #32](https://github.com/Hyant4/Knack-II-pc-port/issues/32).

## 0.18.8 — Edição chinesa e validação regional

- Perfil experimental CUSA08096 01.01 e launcher em chinês simplificado.
- Dumps reconhecidos com versão ou hashes novos podem iniciar com aviso, sem patches específicos de versão.
- Foram executados testes automatizados e verificação do pacote Windows. Boot, legendas e voz ainda precisam de teste com o dump chinês real.
- Esta prévia inclui Windows e fontes. O AppImage 0.18.7 segue disponível na release anterior.

# Knack II PC 0.18.7

## Português

- Restaura banner, imagens do launcher e fotos do guia de controles no pacote.
- Impede empacotamento sem os recursos obrigatórios da interface.
- Corrige acentos nas notas e inclui AppImage Linux x86-64 atualizado.
- Mantém admissão de áudio comprimido até 32 MiB por segmento/voz e teto global de 128 MiB, introduzidos na 0.18.6.

Windows x64, Linux AppImage e fontes estão anexados com SHA-256. Linux continua experimental e requer glibc 2.43+ e driver Vulkan 1.3+. Testes de pacote não certificam gameplay, áudio audível ou compatibilidade universal. A cutscene longa e o retorno visual de quadros continuam sujeitos à validação correspondente.

Nenhum jogo, firmware ou save acompanha os pacotes. Extraia Windows em pasta nova e preserve seus dados de usuário. A 0.18.6 permanece no histórico; esta versão corrige seu empacotamento.

## English

- Restores the launcher hero, gameplay artwork and controller-guide photos in the package.
- Rejects packaging when required UI resources are missing.
- Corrects accented release text and includes an updated Linux x86-64 AppImage.
- Keeps the 32 MiB compressed-segment/per-voice limit and 128 MiB global budget from 0.18.6.

Windows x64, Linux AppImage and sources are attached with SHA-256. Linux remains experimental and requires glibc 2.43+ and a Vulkan 1.3+ driver. Package tests do not certify gameplay, audible audio or universal compatibility. The long cutscene and visual frame rollback still require their respective validation.

No game, firmware or save data is included. Extract Windows into a fresh directory and preserve your user data. Version 0.18.6 stays in history; this version repairs its packaging.

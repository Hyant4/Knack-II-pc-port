# SPDX-License-Identifier: GPL-2.0-or-later
"""Local package installation UI catalog; native diagnostic detail stays verbatim."""

CATALOG = {
    'Extrair jogo-base (.pkg)': ('Extract base game (.pkg)', 'Extraer juego base (.pkg)', 'Extraire le jeu de base (.pkg)', 'Извлечь основную игру (.pkg)'),
    'Selecione o PKG do jogo-base': ('Select the base-game PKG', 'Selecciona el PKG del juego base', 'Sélectionnez le PKG du jeu de base', 'Выберите PKG основной игры'),
    'Extrair jogo na pasta do PKG?': ('Extract the game into the PKG folder?', '¿Extraer el juego en la carpeta del PKG?', 'Extraire le jeu dans le dossier du PKG ?', 'Извлечь игру в папку PKG?'),
    'Cancelar operação PKG': ('Cancel PKG operation', 'Cancelar operación PKG', 'Annuler l’opération PKG', 'Отменить операцию с PKG'),
    'Instalar update (.pkg)': ('Install update (.pkg)', 'Instalar actualización (.pkg)', 'Installer une mise à jour (.pkg)', 'Установить обновление (.pkg)'),
    'Restaurar backup': ('Restore backup', 'Restaurar copia', 'Restaurer la sauvegarde', 'Восстановить резервную копию'),
    'Abrir backup': ('Open backup', 'Abrir copia', 'Ouvrir la sauvegarde', 'Открыть резервную копию'),
    'Cancelar update': ('Cancel update', 'Cancelar actualización', 'Annuler la mise à jour', 'Отменить обновление'),
    'Selecione o PKG de atualização': ('Select the update PKG', 'Selecciona el PKG de actualización', 'Sélectionnez le PKG de mise à jour', 'Выберите PKG обновления'),
    'Instalar atualização do jogo?': ('Install game update?', '¿Instalar actualización del juego?', 'Installer la mise à jour du jeu ?', 'Установить обновление игры?'),
    'Restaurar arquivos anteriores?': ('Restore previous files?', '¿Restaurar archivos anteriores?', 'Restaurer les fichiers précédents ?', 'Восстановить предыдущие файлы?'),
    'A atualização será desfeita usando o backup. Arquivos alterados depois da atualização bloqueiam a restauração para preservar suas mudanças.': (
        'The update will be undone using the backup. Files changed after the update block restoration to preserve your changes.',
        'La actualización se deshará con la copia. Los archivos modificados después bloquean la restauración para conservar tus cambios.',
        'La mise à jour sera annulée avec la sauvegarde. Les fichiers modifiés ensuite bloquent la restauration pour conserver vos changements.',
        'Обновление будет отменено из резервной копии. Изменённые после обновления файлы блокируют восстановление для сохранения ваших изменений.'),
    'O espaço descompactado será conferido durante a extração. Somente arquivos substituídos terão backup.': (
        'Unpacked space will be checked during extraction. Only replaced files will be backed up.',
        'El espacio descomprimido se comprobará al extraer. Solo se copiarán los archivos sustituidos.',
        'L’espace décompressé sera vérifié pendant l’extraction. Seuls les fichiers remplacés seront sauvegardés.',
        'Место для распакованных файлов будет проверено при извлечении. Сохраняются только заменяемые файлы.'),
    'Analisando atualização…': ('Inspecting update…', 'Analizando actualización…', 'Analyse de la mise à jour…', 'Проверка обновления…'),
    'Atualização pronta para confirmação.': ('Update ready for confirmation.', 'Actualización lista para confirmar.', 'Mise à jour prête à confirmer.', 'Обновление готово к подтверждению.'),
    'Extraindo atualização. Instalação atual preservada.': ('Extracting update. Current installation preserved.', 'Extrayendo actualización. Instalación actual conservada.', 'Extraction. Installation actuelle conservée.', 'Извлечение обновления. Текущая установка сохранена.'),
    'Conferindo arquivos da atualização sobre a instalação atual.': ('Checking update files against the current installation.', 'Comprobando actualización e instalación actual.', 'Vérification des fichiers avec l’installation actuelle.', 'Проверка файлов обновления с текущей установкой.'),
    'Aplicando atualização com backup dos arquivos substituídos.': ('Applying update with backup of replaced files.', 'Aplicando actualización con copia de archivos sustituidos.', 'Application avec sauvegarde des fichiers remplacés.', 'Установка с резервным копированием заменяемых файлов.'),
    'Validando instalação atualizada.': ('Validating updated installation.', 'Validando instalación actualizada.', 'Validation de l’installation mise à jour.', 'Проверка обновлённой установки.'),
    'Atualização concluída. Backup preservado.': ('Update complete. Backup preserved.', 'Actualización completada. Copia conservada.', 'Mise à jour terminée. Sauvegarde conservée.', 'Обновление завершено. Резервная копия сохранена.'),
    'Arquivos anteriores restaurados. Valide a instalação.': ('Previous files restored. Validate the installation.', 'Archivos anteriores restaurados. Valida la instalación.', 'Fichiers précédents restaurés. Validez l’installation.', 'Предыдущие файлы восстановлены. Проверьте установку.'),
    'Cancelando; aguarde a restauração dos arquivos.': ('Canceling; wait for files to be restored.', 'Cancelando; espera la restauración de archivos.', 'Annulation ; attendez la restauration des fichiers.', 'Отмена; дождитесь восстановления файлов.'),
    'Selecione a pasta do jogo antes de escolher o update.': ('Select the game folder before choosing an update.', 'Selecciona la carpeta del juego antes de la actualización.', 'Sélectionnez le dossier du jeu avant la mise à jour.', 'Выберите папку игры перед обновлением.'),
}

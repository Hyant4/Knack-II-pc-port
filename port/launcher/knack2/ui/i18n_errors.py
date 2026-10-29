# SPDX-License-Identifier: GPL-2.0-or-later
"""Presentation-only translations for complete error templates and opaque values.

Existing exceptions remain locale independent. Only recognized complete messages
are rendered; filenames, hashes and other data are never translated as prose.
"""
import re
from string import Formatter

# Source Portuguese, English, Russian. Named fields are opaque except for the
# explicitly nested diagnostic fields listed in NESTED_FIELDS below.
ERROR_MESSAGES = [
    ('Idioma inicial inválido', 'Invalid initial language', 'Недопустимый начальный язык'),
    ('Extraindo itens do jogo: {done}/{total}.', 'Extracting game items: {done}/{total}.', 'Извлечение элементов игры: {done}/{total}.'),
    ('Selecione um PKG do jogo-base; updates e DLC não são aceitos.', 'Select a base-game PKG; updates and DLC are not accepted.', 'Выберите PKG основной игры; обновления и DLC не принимаются.'),
    ('Extração do jogo cancelada.', 'Game extraction cancelled.', 'Извлечение игры отменено.'),
    ('Extração do jogo excedeu seis horas.', 'Game extraction exceeded six hours.', 'Извлечение игры превысило шесть часов.'),
    ('Renomeação sem sobrescrita indisponível neste sistema.', 'Renaming without overwriting is unavailable on this system.', 'В этой системе недоступно переименование без перезаписи.'),
    ('Área de extração inválida.', 'Invalid extraction area.', 'Недопустимая область извлечения.'),
    ('Extração anterior incompleta. Preserve esta pasta: {path}', 'Previous extraction is incomplete. Preserve this folder: {path}', 'Предыдущее извлечение не завершено. Сохраните эту папку: {path}'),
    ('Já existe no destino: {path}. Nenhum arquivo foi sobrescrito.', 'Already exists at the destination: {path}. No files were overwritten.', 'Уже существует в папке назначения: {path}. Файлы не перезаписаны.'),
    ('PKG-base não pode ser um link ou junction.', 'The base PKG cannot be a link or junction.', 'PKG основной игры не может быть ссылкой или точкой соединения.'),
    ('Destino de extração deve ser a pasta do PKG.', 'The extraction destination must be the PKG folder.', 'Папкой назначения должна быть папка PKG.'),
    ('PKG-base alterado após a confirmação.', 'Base PKG changed after confirmation.', 'PKG основной игры изменён после подтверждения.'),
    ('Espaço insuficiente para extrair o jogo ao lado do PKG.', 'Not enough space to extract the game beside the PKG.', 'Недостаточно места для извлечения игры рядом с PKG.'),
    ('PKG-base alterado durante a extração.', 'Base PKG changed during extraction.', 'PKG основной игры изменён во время извлечения.'),
    ('Identidade extraída difere do PKG-base.', 'Extracted identity differs from the base PKG.', 'Идентификатор извлечённой игры отличается от PKG.'),
    ('Conteúdo extraído inválido.', 'Invalid extracted content.', 'Недопустимое извлечённое содержимое.'),
    ('Extrair jogo-base (.pkg)', 'Extract base game (.pkg)', 'Извлечь основную игру (.pkg)'),
    ('Selecione o PKG do jogo-base', 'Select the base-game PKG', 'Выберите PKG основной игры'),
    ('Extrair jogo na pasta do PKG?', 'Extract the game into the PKG folder?', 'Извлечь игру в папку PKG?'),
    ('Arquivos extraídos diretamente na pasta do PKG:', 'Files extracted directly into the PKG folder:', 'Файлы будут извлечены непосредственно в папку PKG:'),
    ('O PKG original será preservado. Arquivos existentes não serão sobrescritos.', 'The original PKG will be preserved. Existing files will not be overwritten.', 'Исходный PKG будет сохранён. Существующие файлы не будут перезаписаны.'),
    ('Analisando PKG do jogo-base…', 'Inspecting base-game PKG…', 'Проверка PKG основной игры…'),
    ('Jogo-base pronto para extração.', 'Base game ready for extraction.', 'Основная игра готова к извлечению.'),
    ('Jogo-base extraído na pasta do PKG. Valide para jogar.', 'Base game extracted into the PKG folder. Validate to play.', 'Основная игра извлечена в папку PKG. Выполните проверку для запуска.'),
    ('Cancelar operação PKG', 'Cancel PKG operation', 'Отменить операцию с PKG'),
    ('Cancelando extração; aguarde a limpeza dos arquivos temporários.', 'Cancelling extraction; wait for temporary files to be cleaned up.', 'Отмена извлечения; дождитесь удаления временных файлов.'),
    ('Conferindo o PKG-base…', 'Checking base PKG…', 'Проверка PKG основной игры…'),
    ('Extraindo jogo-base na pasta do PKG…', 'Extracting base game into the PKG folder…', 'Извлечение основной игры в папку PKG…'),
    ('Conferindo arquivos extraídos…', 'Checking extracted files…', 'Проверка извлечённых файлов…'),
    ('Concluindo extração na pasta do PKG…', 'Finishing extraction into the PKG folder…', 'Завершение извлечения в папку PKG…'),
    ('Não foi possível concluir a operação.\nDetalhes técnicos: {diagnostic}', 'The operation could not be completed.\nTechnical details: {diagnostic}', 'Не удалось завершить операцию.\nТехнические подробности: {diagnostic}'),
    ('Falha de validação: {detail}\nMotivo: {reason}', 'Validation failed: {detail}\nReason: {reason}', 'Ошибка проверки: {detail}\nПричина: {reason}'),
    ('A pasta selecionada não contém todos os arquivos obrigatórios do dump.', 'The selected folder does not contain all required dump files.', 'В выбранной папке отсутствуют обязательные файлы дампа.'),
    ('O título ou a versão instalada não pertence a um perfil suportado pelo launcher.', 'The installed title or version has no profile supported by this launcher.', 'Установленная игра или её версия не поддерживается профилями лаунчера.'),
    ('Um executável não corresponde ao dump validado para este perfil.', 'An executable does not match the validated dump for this profile.', 'Исполняемый файл не соответствует проверенному дампу этого профиля.'),
    ('Um módulo obrigatório está ausente, corrompido ou não pôde ser lido.', 'A required module is missing, corrupt or unreadable.', 'Обязательный модуль отсутствует, повреждён или недоступен для чтения.'),
    ('O perfil local do launcher está incompleto ou incompatível.', 'The local launcher profile is incomplete or incompatible.', 'Локальный профиль лаунчера неполон или несовместим.'),
    ('A verificação não conseguiu comprovar que este dump é compatível.', 'The check could not confirm that this dump is compatible.', 'Проверка не смогла подтвердить совместимость дампа.'),
    ('Perfil não suportado: {profile}', 'Unsupported profile: {profile}', 'Неподдерживаемый профиль: {profile}'),
    ('Layout de executável incompatível com o perfil selecionado', 'Executable layout is incompatible with the selected profile', 'Структура исполняемого файла несовместима с выбранным профилем'),
    ('Perfil inválido: deve ser um objeto JSON (dicionário), obtido: {type}', 'Invalid profile: expected a JSON object (dictionary), got: {type}', 'Неверный профиль: ожидается объект JSON (словарь), получен: {type}'),
    ("Perfil malformado: 'title_id' ausente ou não é uma string válida", "Malformed profile: 'title_id' is missing or is not a valid string", "Неверный профиль: 'title_id' отсутствует или не является допустимой строкой"),
    ("Perfil malformado: 'version' ausente ou não é uma string válida", "Malformed profile: 'version' is missing or is not a valid string", "Неверный профиль: 'version' отсутствует или не является допустимой строкой"),
    ("Perfil malformado: 'executables' deve ser um dicionário não vazio", "Malformed profile: 'executables' must be a nonempty dictionary", "Неверный профиль: 'executables' должен быть непустым словарём"),
    ("Perfil incompleto: executável obrigatório '{file}' ausente no perfil", "Incomplete profile: required executable '{file}' is missing from the profile", "Неполный профиль: обязательный исполняемый файл '{file}' отсутствует в профиле"),
    ("Perfil malformado: hash de '{file}' deve ser uma string SHA-256 hexadecimal de 64 caracteres", "Malformed profile: the hash of '{file}' must be a 64-character hexadecimal SHA-256 string", "Неверный профиль: хеш '{file}' должен быть шестнадцатеричной строкой SHA-256 из 64 символов"),
    ('Variantes de game.elf devem ser uma lista', 'game.elf variants must be a list', 'Варианты game.elf должны быть списком'),
    ('Variante de game.elf malformada', 'Malformed game.elf variant', 'Неверный формат варианта game.elf'),
    ('Hash de variante inválido ou duplicado', 'Invalid or duplicate variant hash', 'Хеш варианта недопустим или повторяется'),
    ('Layout de variante inválido', 'Invalid variant layout', 'Недопустимая структура варианта'),
    ("Perfil malformado: 'modules' deve ser um dicionário mapeando executáveis para módulos", "Malformed profile: 'modules' must map executables to modules in a dictionary", "Неверный профиль: словарь 'modules' должен сопоставлять исполняемые файлы с модулями"),
    ("Perfil incompleto: lista de módulos de '{file}' ausente ou vazia", "Incomplete profile: the module list for '{file}' is missing or empty", "Неполный профиль: список модулей для '{file}' отсутствует или пуст"),
    ("Perfil malformado: módulo inválido na lista de '{file}': {value}", "Malformed profile: invalid module in the list for '{file}': {value}", "Неверный профиль: недопустимый модуль в списке '{file}': {value}"),
    ('Submódulo prepare indisponível para validação do dump (fail-closed)', 'The prepare component is unavailable for dump validation (fail-closed)', 'Компонент prepare недоступен для проверки дампа (fail-closed)'),
    ('Submódulo link_modules indisponível para validação de módulos ELF (fail-closed)', 'The link_modules component is unavailable for ELF module validation (fail-closed)', 'Компонент link_modules недоступен для проверки модулей ELF (fail-closed)'),
    ('Diretório do jogo não encontrado: {path}', 'Game directory not found: {path}', 'Папка игры не найдена: {path}'),
    ('Arquivo de metadados não encontrado: {path}', 'Metadata file not found: {path}', 'Файл метаданных не найден: {path}'),
    ('Instalação não suportada: {detail}', 'Unsupported installation: {detail}', 'Неподдерживаемая установка: {detail}'),
    ('Arquivo de perfil não encontrado: {path}', 'Profile file not found: {path}', 'Файл профиля не найден: {path}'),
    ('Falha ao ler arquivo de perfil {path}: {detail}', 'Could not read profile file {path}: {detail}', 'Не удалось прочитать файл профиля {path}: {detail}'),
    ('Identidade do perfil difere do arquivo selecionado', 'Profile identity differs from the selected file', 'Идентификатор профиля не соответствует выбранному файлу'),
    ('param.sfo inválido ou corrompido: {detail}', 'Invalid or corrupt param.sfo: {detail}', 'Файл param.sfo недопустим или повреждён: {detail}'),
    ('Este perfil requer {title} versão {version} (obtido: {actual_title} {actual_version})', 'This profile requires {title} version {version} (got: {actual_title} {actual_version})', 'Для этого профиля требуется {title} версии {version} (получено: {actual_title} {actual_version})'),
    ('{file}: caminho inválido ou fora do dump: {detail}', '{file}: invalid path or path outside the dump: {detail}', '{file}: путь недопустим или находится вне дампа: {detail}'),
    ('Executável ausente no dump: {path}', 'Executable missing from dump: {path}', 'В дампе отсутствует исполняемый файл: {path}'),
    ('{file}: SHA-256 difere do dump suportado. Encontrado: {actual}. Esperado: {expected}.', '{file}: SHA-256 differs from the supported dump. Found: {actual}. Expected: {expected}.', '{file}: SHA-256 не соответствует поддерживаемому дампу. Получено: {actual}. Ожидается: {expected}.'),
    ('game.elf: SHA-256 difere do dump suportado para {profile}. Encontrado: {actual}. Aceitos: {accepted}.', 'game.elf: SHA-256 differs from the supported dump for {profile}. Found: {actual}. Accepted: {accepted}.', 'game.elf: SHA-256 не соответствует поддерживаемому дампу для {profile}. Получено: {actual}. Допустимые: {accepted}.'),
    ('{file}: SHA-256 difere do dump suportado', '{file}: SHA-256 differs from the supported dump', '{file}: SHA-256 не соответствует поддерживаемому дампу'),
    ('Módulo essencial ausente: sce_module/{file}', 'Required module missing: sce_module/{file}', 'Обязательный модуль отсутствует: sce_module/{file}'),
    ('Módulo inválido ou incompatível ({file}): {detail}', 'Invalid or incompatible module ({file}): {detail}', 'Модуль недопустим или несовместим ({file}): {detail}'),
    ('Caminho inválido no update.', 'Invalid path in update.', 'Недопустимый путь в обновлении.'),
    ('Caminho inválido no update: {path}', 'Invalid path in update: {path}', 'Недопустимый путь в обновлении: {path}'),
    ('Link ou junction não permitido: {path}', 'Link or junction is not allowed: {path}', 'Ссылка или junction не допускается: {path}'),
    ('Caminho fora da instalação: {path}', 'Path outside the installation: {path}', 'Путь находится вне установки: {path}'),
    ('PKG incompleto ou tabela fora dos limites.', 'Incomplete PKG or table out of bounds.', 'PKG неполон или таблица выходит за допустимые границы.'),
    ('Falha de leitura do PKG.', 'Could not read PKG.', 'Не удалось прочитать PKG.'),
    ('Arquivo não é um PKG de PS4.', 'The file is not a PS4 PKG.', 'Файл не является PKG для PS4.'),
    ('PKG incompleto ou formato não suportado.', 'PKG is incomplete or its format is unsupported.', 'PKG неполон или его формат не поддерживается.'),
    ('Entrada PKG fora dos limites suportados.', 'PKG entry exceeds supported bounds.', 'Запись PKG выходит за поддерживаемые границы.'),
    ('Metadados PKG duplicados ou grandes demais.', 'PKG metadata is duplicated or too large.', 'Метаданные PKG повторяются или имеют слишком большой размер.'),
    ('Selecione um PKG de atualização; base e DLC não são aceitos.', 'Select an update PKG; base games and DLC are not accepted.', 'Выберите PKG обновления; основная игра и DLC не принимаются.'),
    ('TITLE_ID inválido no update.', 'Invalid TITLE_ID in update.', 'Недопустимый TITLE_ID в обновлении.'),
    ('CUSA/CONTENT_ID do PKG e param.sfo são inconsistentes.', 'PKG and param.sfo have inconsistent CUSA/CONTENT_ID values.', 'Значения CUSA/CONTENT_ID в PKG и param.sfo не совпадают.'),
    ('Versão inválida nos metadados.', 'Invalid version in metadata.', 'Недопустимая версия в метаданных.'),
    ('Update incompatível: jogo {title}; PKG {pkg_title}. Use atualização do mesmo CUSA.', 'Incompatible update: game {title}; PKG {pkg_title}. Use an update with the same CUSA.', 'Несовместимое обновление: игра {title}; PKG {pkg_title}. Используйте обновление с тем же CUSA.'),
    ('CONTENT_ID do jogo difere do update. Instalação preservada.', 'The game and update have different CONTENT_ID values. Installation preserved.', 'CONTENT_ID игры и обновления различаются. Установка сохранена.'),
    ('Update já instalado ou mais antigo. Nenhum arquivo alterado.', 'The update is already installed or is older. No files changed.', 'Обновление уже установлено или имеет более старую версию. Файлы не изменены.'),
    ('Pasta do jogo inválida.', 'Invalid game folder.', 'Недопустимая папка игры.'),
    ('Lock de instalação redirecionado.', 'Installation lock was redirected.', 'Блокировка установки перенаправлена.'),
    ('Instalação em uso por outro launcher ou atualização.', 'Another launcher or update is using this installation.', 'Установка используется другим лаунчером или обновлением.'),
    ('Área de atualização incompleta. Preserve-a e consulte o diagnóstico.', 'The update area is incomplete. Preserve it and consult diagnostics.', 'Область обновления неполна. Сохраните её и проверьте диагностику.'),
    ('Registro de recuperação inválido. Nenhum arquivo alterado.', 'Invalid recovery record. No files changed.', 'Недопустимая запись восстановления. Файлы не изменены.'),
    ('Registro de recuperação inválido.', 'Invalid recovery record.', 'Недопустимая запись восстановления.'),
    ('Atualização interrompida. Restaure o backup na tela Instalação antes de jogar.', 'Update interrupted. Restore the backup on the Installation screen before playing.', 'Обновление прервано. Перед игрой восстановите резервную копию на экране «Установка».'),
    ('Atualização cancelada.', 'Update canceled.', 'Обновление отменено.'),
    ('Extrator PKG ausente. Use um pacote do launcher com KnackPkg incluído.', 'PKG extractor missing. Use a launcher package that includes KnackPkg.', 'Экстрактор PKG отсутствует. Используйте пакет лаунчера с KnackPkg.'),
    ('Extração excedeu 30 minutos. Instalação anterior preservada.', 'Extraction exceeded 30 minutes. Previous installation preserved.', 'Извлечение превысило 30 минут. Предыдущая установка сохранена.'),
    ('Falha ao extrair PKG: {detail}', 'Could not extract PKG: {detail}', 'Не удалось извлечь PKG: {detail}'),
    ('Extrator não confirmou a conclusão da atualização.', 'The extractor did not confirm update completion.', 'Экстрактор не подтвердил завершение обновления.'),
    ('PKG mudou após a análise. Selecione o arquivo novamente.', 'PKG changed after inspection. Select the file again.', 'PKG изменился после проверки. Выберите файл заново.'),
    ('Restaure a atualização interrompida antes de tentar novamente.', 'Restore the interrupted update before trying again.', 'Восстановите прерванное обновление перед повторной попыткой.'),
    ('Espaço insuficiente para iniciar extração do update.', 'Not enough free space to start extracting the update.', 'Недостаточно места для начала извлечения обновления.'),
    ('PKG mudou durante a extração.', 'PKG changed during extraction.', 'PKG изменился во время извлечения.'),
    ('Metadados extraídos diferem do PKG analisado.', 'Extracted metadata differs from the inspected PKG.', 'Извлечённые метаданные отличаются от проверенного PKG.'),
    ('Espaço livre abaixo da margem para aplicar update.', 'Free space is below the margin required to apply the update.', 'Свободного места меньше требуемого запаса для установки обновления.'),
    ('Arquivo da instalação mudou: {path}', 'Installation file changed: {path}', 'Файл установки изменился: {path}'),
    ('Arquivo temporário mudou: {path}', 'Temporary file changed: {path}', 'Временный файл изменился: {path}'),
    ('Falha: {error}. Recuperação pendente: {recovery}. Preserve a pasta {path}.', 'Failure: {error}. Recovery pending: {recovery}. Preserve the folder {path}.', 'Ошибка: {error}. Требуется восстановление: {recovery}. Сохраните папку {path}.'),
    ('Pasta temporária redirecionada.', 'Temporary folder was redirected.', 'Временная папка перенаправлена.'),
    ('Nomes de arquivos ambíguos no update.', 'Ambiguous filenames in update.', 'Неоднозначные имена файлов в обновлении.'),
    ('Update contém arquivos demais.', 'The update contains too many files.', 'Обновление содержит слишком много файлов.'),
    ('Conflito entre pasta e arquivo: {path}', 'Folder and file conflict: {path}', 'Конфликт папки и файла: {path}'),
    ('Tipo de arquivo incompatível: {path}', 'Incompatible file type: {path}', 'Несовместимый тип файла: {path}'),
    ('Update excede limite de 64 GiB.', 'Update exceeds the 64 GiB limit.', 'Обновление превышает предел 64 GiB.'),
    ('Backup alterado: {path}', 'Backup changed: {path}', 'Резервная копия изменена: {path}'),
    ('Arquivo alterado após o update: {path}', 'File changed after update: {path}', 'Файл изменён после обновления: {path}'),
    ('Arquivo novo foi alterado: {path}', 'New file was modified: {path}', 'Новый файл был изменён: {path}'),
    ('Backup ausente: {path}', 'Backup missing: {path}', 'Резервная копия отсутствует: {path}'),
    ('Nenhuma atualização para restaurar.', 'No update to restore.', 'Нет обновления для восстановления.'),
    ("Valor booleano inválido para '{key}': '{value}' (esperado 0/1)", "Invalid boolean value for '{key}': '{value}' (expected 0/1)", "Недопустимое логическое значение для '{key}': '{value}' (ожидается 0/1)"),
    ("Valor inteiro inválido para '{key}': '{value}'", "Invalid integer value for '{key}': '{value}'", "Недопустимое целое значение для '{key}': '{value}'"),
    ("Valor decimal inválido para '{key}': '{value}'", "Invalid decimal value for '{key}': '{value}'", "Недопустимое десятичное значение для '{key}': '{value}'"),
    ("Valor fora do intervalo permitido para '{key}': {value} (esperado {minimum}..{maximum})", "Value out of range for '{key}': {value} (expected {minimum}..{maximum})", "Значение вне диапазона для '{key}': {value} (ожидается {minimum}..{maximum})"),
    ("Valor fora do intervalo permitido para '{key}': {value} (esperado {expected})", "Value out of range for '{key}': {value} (expected {expected})", "Значение вне диапазона для '{key}': {value} (ожидается {expected})"),
    ('A chave de configuração deve ser uma string: {value}', 'The configuration key must be a string: {value}', 'Ключ настройки должен быть строкой: {value}'),
    ("Caracteres de controle inválidos na chave bruta: '{key}'", "Invalid control characters in raw key: '{key}'", "Недопустимые управляющие символы в исходном ключе: '{key}'"),
    ("Chave de configuração não autorizada para o launcher: '{key}'", "Configuration key is not allowed in this launcher: '{key}'", "Ключ настройки не разрешён в этом лаунчере: '{key}'"),
    ("Tipo de valor inválido para '{key}': {value}", "Invalid value type for '{key}': {value}", "Недопустимый тип значения для '{key}': {value}"),
    ("Caracteres de controle ou quebra de linha inválidos no valor bruto de '{key}'", "Invalid control characters or line breaks in the raw value of '{key}'", "Недопустимые управляющие символы или переносы строк в исходном значении '{key}'"),
    ("Diagnóstico GPU inválido para '{key}': '{value}' (esperado auto/0/1)", "Invalid GPU diagnostic value for '{key}': '{value}' (expected auto/0/1)", "Недопустимое значение диагностики GPU для '{key}': '{value}' (ожидается auto/0/1)"),
    ("Suavização inválida: '{value}'", "Invalid antialiasing: '{value}'", "Недопустимое сглаживание: '{value}'"),
    ("FSR temporal inválido: '{value}'", "Invalid temporal FSR: '{value}'", "Недопустимое временное FSR: '{value}'"),
    ("Resolução interna inválida: '{value}'", "Invalid internal resolution: '{value}'", "Недопустимое внутреннее разрешение: '{value}'"),
    ("Resolução de janela inválida: '{value}'", "Invalid window resolution: '{value}'", "Недопустимое разрешение окна: '{value}'"),
    ("Proporção inválida: '{value}'", "Invalid aspect ratio: '{value}'", "Недопустимое соотношение сторон: '{value}'"),
    ("Modo de exibição inválido: '{value}' (esperado windowed, borderless ou fullscreen)", "Invalid display mode: '{value}' (expected windowed, borderless or fullscreen)", "Недопустимый режим отображения: '{value}' (ожидается windowed, borderless или fullscreen)"),
    ("O mapeamento de teclado 'key_map' excede o limite máximo de 1024 caracteres", "Keyboard mapping 'key_map' exceeds the 1024-character limit", "Раскладка клавиш 'key_map' превышает предел 1024 символа"),
    ('Para desligar a suavização nativa, desligue primeiro o FSR temporal.', 'To disable native antialiasing, disable temporal FSR first.', 'Чтобы отключить родное сглаживание, сначала отключите временное FSR.'),
    ('Suavização desligada requer resolução interna 1080p, 1440p ou 4K.', 'Disabling antialiasing requires internal resolution 1080p, 1440p or 4K.', 'Для отключения сглаживания требуется внутреннее разрешение 1080p, 1440p или 4K.'),
    ('O parâmetro game_dir é obrigatório', 'The game_dir parameter is required', 'Параметр game_dir обязателен'),
    ('Supervisor run.py não encontrado: {path}', 'Supervisor run.py not found: {path}', 'Супервизор run.py не найден: {path}'),
    ('O timeout deve ser inteiro >= 0 (0 = sem limite), obtido: {value}', 'Timeout must be an integer >= 0 (0 = no limit), got: {value}', 'Тайм-аут должен быть целым числом >= 0 (0 = без ограничения), получено: {value}'),
    ('Selecione um ou dois jogadores.', 'Select one or two players.', 'Выберите одного или двух игроков.'),
    ('P2 exige ID entre 2 e 2147483647 e dois jogadores explícitos.', 'P2 requires an ID between 2 and 2147483647 and an explicit two-player selection.', 'Для P2 требуется ID от 2 до 2147483647 и явный выбор двух игроков.'),
    ('P2 exige saída isolada explícita.', 'P2 requires an explicit isolated output directory.', 'Для P2 требуется явно заданная отдельная папка вывода.'),
    ('Arquivo INI não encontrado.', 'INI file not found.', 'Файл INI не найден.'),
    ('analyze_only e check_imports são mutuamente exclusivos', 'analyze_only and check_imports are mutually exclusive', 'analyze_only и check_imports взаимно исключают друг друга'),
    ('Supervisor já está em execução (estado atual: {state})', 'Supervisor is already running (current state: {state})', 'Супервизор уже запущен (текущее состояние: {state})'),
    ('Falha ao iniciar processo supervisor: {detail}', 'Could not start the supervisor process: {detail}', 'Не удалось запустить процесс супервизора: {detail}'),
    ('Processo supervisor encerrou com código de saída {code}', 'Supervisor process exited with code {code}', 'Процесс супервизора завершился с кодом {code}'),
    ('O processo supervisor {pid} não encerrou após período de graça ({grace}s) e escalação forçada ({forced}s).', 'Supervisor process {pid} did not exit after the grace period ({grace}s) and forced termination ({forced}s).', 'Процесс супервизора {pid} не завершился после периода ожидания ({grace}s) и принудительного завершения ({forced}s).'),
    ('Não foi possível fechar o runner: o processo supervisor {pid} ainda está ativo.', 'Could not close the runner: supervisor process {pid} is still active.', 'Не удалось закрыть runner: процесс супервизора {pid} ещё активен.'),
    ('Thread de monitoramento de saída anterior ({thread}) ainda está ativa após o tempo limite de espera.', 'Previous output monitor thread ({thread}) is still active after the wait timeout.', 'Предыдущий поток контроля завершения ({thread}) всё ещё активен после истечения времени ожидания.'),
    ('Thread de leitura de logs anterior ({thread}) ainda está ativa após o tempo limite de espera.', 'Previous log reader thread ({thread}) is still active after the wait timeout.', 'Предыдущий поток чтения журналов ({thread}) всё ещё активен после истечения времени ожидания.'),
    ('Foram encontrados saves antigos de solo/coop. Importe o save que deseja manter antes de jogar; nenhum arquivo foi sobrescrito.', 'Older solo/co-op saves were found. Import the save you want to keep before playing; no files were overwritten.', 'Найдены старые сохранения одиночной и совместной игры. Перед игрой импортируйте нужное сохранение; файлы не перезаписаны.'),
    ('Este perfil de saves já está em uso. Encerre a outra sessão.', 'This save profile is already in use. Close the other session.', 'Этот профиль сохранений уже используется. Завершите другой сеанс.'),
    ('Knack II: mantenha o launcher junto ao repositório do projeto.', 'Knack II: keep the launcher beside the project repository.', 'Knack II: держите лаунчер рядом с репозиторием проекта.'),
    ('Supervisor Knack II ausente. Reinstale o build completo do launcher.', 'Knack II supervisor missing. Reinstall the complete launcher build.', 'Супервизор Knack II отсутствует. Переустановите полную сборку лаунчера.'),
    ('Opção indisponível nesta interface.', 'Option unavailable in this interface.', 'Параметр недоступен в этом интерфейсе.'),
    ('Intensidade RCAS inválida.', 'Invalid RCAS strength.', 'Недопустимая интенсивность RCAS.'),
    ('Área F3B sobrepõe dump ou dados habituais. Escolha outro --data-dir.', 'The F3B workspace overlaps the dump or existing data. Choose another --data-dir.', 'Рабочая папка F3B пересекается с дампом или существующими данными. Выберите другой --data-dir.'),
    ('Caminho de saída redirecionado para fora da área F3B.', 'Output path was redirected outside the F3B workspace.', 'Путь вывода перенаправлен за пределы рабочей папки F3B.'),
    ('Runtime ausente: compile tlg-probe antes de jogar.', 'Runtime missing: build tlg-probe before playing.', 'Среда выполнения отсутствует: соберите tlg-probe перед игрой.'),
    ('Saída P2 precisa ser nova e ter caminho inferior a 512 bytes.', 'P2 output must be new and its path must be shorter than 512 bytes.', 'Папка вывода P2 должна быть новой, а путь — короче 512 байт.'),
    ('Caminho do perfil de saves inválido.', 'Invalid save profile path.', 'Недопустимый путь профиля сохранений.'),
    ('Não foi possível abrir a pasta de logs.', 'Could not open the log folder.', 'Не удалось открыть папку журналов.'),
    ('Validação concluída, mas não foi possível registrar o resultado: {detail}', 'Validation completed, but its result could not be saved: {detail}', 'Проверка завершена, но сохранить её результат не удалось: {detail}'),
    ('Falha ao gravar log do supervisor: {detail}', 'Could not write the supervisor log: {detail}', 'Не удалось записать журнал супервизора: {detail}'),
    ('Não iniciou: {reason}', 'Did not start: {reason}', 'Запуск отменён: {reason}'),
    ('Não iniciou automaticamente: {reason}', 'Did not start automatically: {reason}', 'Автоматический запуск отменён: {reason}'),
    ('a validação automática encontrou o erro acima.', 'automatic validation found the error above.', 'автоматическая проверка обнаружила указанную выше ошибку.'),
    ('nenhuma pasta de dump foi salva. Escolha uma pasta e valide-a primeiro.', 'no dump folder was saved. Choose a folder and validate it first.', 'папка дампа не сохранена. Выберите папку и сначала проверьте её.'),
    ('a validação automática ainda está em andamento. Aguarde o resultado.', 'automatic validation is still running. Wait for the result.', 'автоматическая проверка ещё выполняется. Дождитесь результата.'),
    ('já existe uma sessão em andamento.', 'a session is already running.', 'сеанс уже запущен.'),
    ('nenhuma instalação válida foi confirmada.', 'no valid installation was confirmed.', 'не подтверждена ни одна допустимая установка.'),
    ('este dump ainda não tinha uma validação salva antes desta abertura. A validação atual foi concluída; clique em Jogar para confirmar.', 'this dump had no saved validation before this launch. The current check is complete; click Play to confirm.', 'до этого запуска для дампа не было сохранённой проверки. Текущая проверка завершена; нажмите «Играть» для подтверждения.'),
    ('o dump validado anteriormente não é o mesmo título, versão ou pasta da instalação atual. Revise o resultado e clique em Jogar para confirmar.', 'the previously validated dump has a different title, version or folder from the current installation. Review the result and click Play to confirm.', 'ранее проверенный дамп отличается игрой, версией или папкой от текущей установки. Проверьте результат и нажмите «Играть» для подтверждения.'),
    ('A simulação anterior ainda está ativa.', 'The previous simulation is still active.', 'Предыдущая симуляция ещё активна.'),
    ('A thread sintética não encerrou no prazo.', 'The synthetic thread did not stop within the time limit.', 'Синтетический поток не завершился в установленный срок.'),
    ('Erro simulado: sessão interrompida para testar a interface.', 'Simulated error: session interrupted to test the interface.', 'Тестовая ошибка: сеанс прерван для проверки интерфейса.'),
    ('Instalação alterada; nova validação obrigatória.', 'Installation changed; a new validation is required.', 'Установка изменена; требуется повторная проверка.'),
    ('Configurações salvas na área temporária da demonstração. Serão descartadas ao fechar.', 'Settings saved in the temporary demo workspace. They will be discarded on exit.', 'Настройки сохранены во временной папке демонстрации. При выходе они будут удалены.'),
    ('Não foi possível carregar a interface QML do Knack II.', 'Could not load the Knack II QML interface.', 'Не удалось загрузить интерфейс QML Knack II.'),
    ('Qt/PySide6 indisponível. Instale PySide6-Essentials>=6.7 no ambiente Python do projeto. Nenhum jogo foi iniciado.', 'Qt/PySide6 unavailable. Install PySide6-Essentials>=6.7 in the project Python environment. No game was started.', 'Qt/PySide6 недоступен. Установите PySide6-Essentials>=6.7 в окружение Python проекта. Игра не была запущена.'),
    ('--quit-after-ms deve ser >= 0', '--quit-after-ms must be >= 0', '--quit-after-ms должен быть >= 0'),
    ('--screenshot requer --quit-after-ms', '--screenshot requires --quit-after-ms', '--screenshot требует --quit-after-ms'),
    ('--play não está disponível na demonstração', '--play is unavailable in demo mode', '--play недоступен в демонстрации'),
    ('Falha ao salvar screenshot.', 'Could not save screenshot.', 'Не удалось сохранить снимок экрана.'),
]

# These messages originate in English from configuration I/O or the native helper.
ENGLISH_MESSAGES = [
    ('Unsupported PKG inspection category', 'Tipo de PKG não suportado', 'Неподдерживаемый тип PKG'),
    ('Base mode does not accept update PKGs', 'Modo de jogo-base não aceita PKGs de update', 'Режим основной игры не принимает PKG обновлений'),
    ('Failed to save launcher settings to {path}: {detail}', 'Falha ao salvar configurações do launcher em {path}: {detail}', 'Не удалось сохранить настройки лаунчера в {path}: {detail}'),
    ('Configuration file not found: {path}', 'Arquivo de configuração não encontrado: {path}', 'Файл настроек не найден: {path}'),
    ('Cannot read configuration {path}: {detail}', 'Falha ao ler configuração {path}: {detail}', 'Не удалось прочитать настройки {path}: {detail}'),
    ('Failed to atomically write configuration to {path}: {detail}', 'Falha ao gravar configuração atomicamente em {path}: {detail}', 'Не удалось атомарно записать настройки в {path}: {detail}'),
    ('PKG extraction failed: {detail}', 'Falha na extração do PKG: {detail}', 'Ошибка извлечения PKG: {detail}'),
    ('Not enough free space for unpacked update', 'Espaço insuficiente para o update descompactado', 'Недостаточно места для распакованного обновления'),
    ('Unsupported encrypted PKG key', 'Chave criptográfica do PKG não suportada', 'Ключ шифрования PKG не поддерживается'),
    ('Staging directory must be empty', 'Pasta temporária precisa estar vazia', 'Временная папка должна быть пустой'),
    ('Truncated PKG', 'PKG truncado', 'PKG обрезан'),
    ('Short PKG read', 'Leitura incompleta do PKG', 'Неполное чтение PKG'),
    ('Short extracted write', 'Gravação incompleta do arquivo extraído', 'Неполная запись извлечённого файла'),
    ('Invalid compressed PFS block', 'Bloco PFS comprimido inválido', 'Недопустимый сжатый блок PFS'),
    ('PKG/PFS range out of bounds', 'Intervalo PKG/PFS fora dos limites', 'Диапазон PKG/PFS выходит за границы'),
    ('Cannot flush extracted file', 'Não foi possível concluir a gravação do arquivo extraído', 'Не удалось завершить запись извлечённого файла'),
    ('Extracted size mismatch', 'Tamanho extraído divergente', 'Размер извлечённого файла не совпадает'),
    ('Content size is bigger than pkg size', 'Conteúdo excede tamanho do PKG', 'Размер содержимого превышает размер PKG'),
    ('Duplicate inode', 'Inode duplicado', 'Повторяющийся inode'),
    ('Duplicate output file', 'Arquivo de saída duplicado', 'Повторяющийся выходной файл'),
    ('Failed to seek to PFS image offset', 'Falha ao acessar offset da imagem PFS', 'Не удалось перейти к смещению образа PFS'),
    ('Failed to seek to PKG entry offset', 'Falha ao acessar offset de entrada PKG', 'Не удалось перейти к смещению записи PKG'),
    ('Failed to seek to PKG table entry offset', 'Falha ao acessar tabela de entradas PKG', 'Не удалось перейти к таблице записей PKG'),
    ('Failed to seek to param.sfo offset', 'Falha ao acessar offset de param.sfo', 'Не удалось перейти к смещению param.sfo'),
    ('Incomplete PFS file table', 'Tabela de arquivos PFS incompleta', 'Неполная таблица файлов PFS'),
    ('Invalid PFS sector map', 'Mapa de setores PFS inválido', 'Недопустимая карта секторов PFS'),
    ('Invalid PFS sector size', 'Tamanho de setor PFS inválido', 'Недопустимый размер сектора PFS'),
    ('Invalid file block', 'Bloco de arquivo inválido', 'Недопустимый блок файла'),
    ('Invalid file block size', 'Tamanho de bloco de arquivo inválido', 'Недопустимый размер блока файла'),
    ('SFO too large', 'SFO grande demais', 'SFO слишком велик'),
    ('Too many PKG entries', 'Entradas demais no PKG', 'Слишком много записей PKG'),
    ('Truncated image key', 'Chave da imagem truncada', 'Ключ образа обрезан'),
    ('Truncated key table', 'Tabela de chaves truncada', 'Таблица ключей обрезана'),
    ('Unaligned PFS cache', 'Cache PFS desalinhado', 'Кеш PFS не выровнен'),
    ('Unsupported PFS cache size', 'Tamanho de cache PFS não suportado', 'Размер кеша PFS не поддерживается'),
    ('Unsupported PFS size', 'Tamanho PFS não suportado', 'Размер PFS не поддерживается'),
    ('Unsupported file size', 'Tamanho de arquivo não suportado', 'Размер файла не поддерживается'),
    ('Unsupported inode block layout', 'Layout de blocos do inode não suportado', 'Структура блоков inode не поддерживается'),
    ('Unsupported inode count', 'Quantidade de inodes não suportada', 'Количество inode не поддерживается'),
    ('Unsupported inode type', 'Tipo de inode não suportado', 'Тип inode не поддерживается'),
    ('Update too large', 'Atualização grande demais', 'Обновление слишком велико'),
    ('Ambiguous filename', 'Nome de arquivo ambíguo', 'Неоднозначное имя файла'),
    ('Destination outside staging', 'Destino fora da pasta temporária', 'Назначение вне временной папки'),
    ('Invalid PFS directory entry', 'Entrada de diretório PFS inválida', 'Недопустимая запись каталога PFS'),
    ('Invalid PFS filename', 'Nome de arquivo PFS inválido', 'Недопустимое имя файла PFS'),
    ('Reparse point in destination', 'Reparse point no destino', 'Точка повторной обработки в пути назначения'),
    ('Reserved device filename', 'Nome de arquivo reservado para dispositivo', 'Имя файла зарезервировано для устройства'),
    ('Reserved filename', 'Nome de arquivo reservado', 'Зарезервированное имя файла'),
    ('Symlink in destination', 'Link simbólico no destino', 'Символическая ссылка в пути назначения'),
    ('Unsafe PFS filename', 'Nome de arquivo PFS inseguro', 'Небезопасное имя файла PFS'),
    ('Cannot extract PKG', 'Não foi possível extrair PKG', 'Не удалось извлечь PKG'),
    ('Cannot inspect PKG', 'Não foi possível analisar PKG', 'Не удалось проверить PKG'),
    ('Only update PKGs are accepted', 'Apenas PKGs de atualização são aceitos', 'Принимаются только PKG обновлений'),
    ('Cannot open extracted file', 'Não foi possível abrir arquivo extraído', 'Не удалось открыть извлечённый файл'),
    ('Short object read', 'Leitura incompleta de objeto', 'Неполное чтение объекта'),
    ('Short write', 'Gravação incompleta', 'Неполная запись'),
    ('PKG file size is different', 'Tamanho do PKG divergente', 'Размер PKG не совпадает'),
    ('PKG metadata too large', 'Metadados do PKG grandes demais', 'Метаданные PKG слишком велики'),
    ('PKG must be outside staging', 'PKG precisa ficar fora da pasta temporária', 'PKG должен находиться вне временной папки'),
]

NESTED_FIELDS = frozenset(('detail', 'reason', 'error', 'recovery'))


def _compile(source):
    pieces, fields = [], []
    for literal, field, spec, conversion in Formatter().parse(source):
        pieces.append(re.escape(literal))
        if field is not None:
            if spec or conversion or not field.isidentifier() or field in fields:
                raise ValueError('Invalid error translation template: ' + source)
            fields.append(field)
            pieces.append('(?P<' + field + '>.*?)')
    return re.compile(''.join(pieces), re.DOTALL)


ENTRIES = [(source, {'pt-BR': source, 'en-US': english, 'ru-RU': russian})
           for source, english, russian in ERROR_MESSAGES]
ENTRIES += [(source, {'pt-BR': portuguese, 'en-US': source, 'ru-RU': russian})
            for source, portuguese, russian in ENGLISH_MESSAGES]
PATTERNS = [(_compile(source), translations) for source, translations in ENTRIES]


def translate_error(locale, source, exact=lambda locale, text: None, *, depth=0):
    """Render a complete known template; never substitute words inside data."""
    if depth > 8:
        return None
    language = 'ru-RU' if locale == 'ru-RU' else 'pt-BR' if locale in ('pt-BR', 'pt-PT') else 'en-US'
    for pattern, translations in PATTERNS:
        match = pattern.fullmatch(source)
        if match is None:
            continue
        values = match.groupdict()
        for field in NESTED_FIELDS.intersection(values):
            raw = values[field]
            core = raw.strip()
            translated = exact(locale, core)
            if translated is None:
                translated = translate_error(locale, core, exact, depth=depth + 1)
            if translated is not None:
                start = len(raw) - len(raw.lstrip())
                values[field] = raw[:start] + translated + raw[start + len(core):]
        return translations[language].format_map(values)
    return None

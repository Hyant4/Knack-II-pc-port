# SPDX-License-Identifier: GPL-2.0-or-later
"""Local UI catalogs. Runtime logs and external diagnostic errors remain verbatim."""
from PySide6.QtCore import QTranslator
from functools import lru_cache
from .i18n_ru import RUSSIAN
from .i18n_update import CATALOG as UPDATE_MESSAGES
from .i18n_errors import translate_error

# Source Portuguese, followed by English, Latin American Spanish and French.
MESSAGES = {
    'Preparação': ['Preparation', 'Preparación', 'Préparation'],
    'Código': ['Exit code', 'Código de salida', 'Code de sortie'],
    'Logs do supervisor': ['Supervisor logs', 'Registros del supervisor', 'Journaux du superviseur'],
    'Logs do jogo': ['Game logs', 'Registros del juego', 'Journaux du jeu'],
    'aguardando sessão': ['waiting for session', 'esperando sesión', 'en attente de session'],
    'Sessão ambígua; consulte a saída do supervisor.': ['Ambiguous session; check supervisor output.', 'Sesión ambigua; consulta la salida del supervisor.', 'Session ambiguë ; consultez la sortie du superviseur.'],
    'Build experimental: arquivos conferidos; compatibilidade de jogo não verificada.': ['Experimental build: files checked; gameplay compatibility is unverified.', 'Compilación experimental: archivos comprobados; compatibilidad del juego sin verificar.', 'Version expérimentale : fichiers vérifiés ; compatibilité du jeu non vérifiée.'],
    'GTX 1060: desempenho': ['GTX 1060: performance', 'GTX 1060: rendimiento', 'GTX 1060 : performances'],
    'Ajustes relatados na issue #12 para GTX 1060 6 GB; o desempenho varia conforme a cena e o computador.': [
        'Settings reported in issue #12 for GTX 1060 6 GB; performance varies by scene and computer.',
        'Ajustes reportados en la issue #12 para GTX 1060 de 6 GB; el rendimiento depende de la escena y del equipo.',
        'Réglages signalés dans le ticket #12 pour GTX 1060 6 Go ; les performances varient selon la scène et le PC.'],
    'Usa 1080p interno, FSR temporal desligado e GPU Rápida, sem partículas da GPU. Reduz detalhes da destruição. Salve e reinicie o jogo.': [
        'Uses internal 1080p, temporal FSR off and Fast GPU accuracy, without GPU particles. Reduces destruction detail. Save and restart the game.',
        'Usa 1080p interno, FSR temporal desactivado y precisión de GPU Rápida, sin partículas de GPU. Reduce detalles de destrucción. Guarda y reinicia el juego.',
        'Utilise 1080p interne, FSR temporel désactivé et précision GPU Rapide, sans particules GPU. Réduit les détails de destruction. Enregistrez et relancez le jeu.'],
    'Confira os ajustes descritos em cada perfil. O preset de desempenho GTX 1060 altera resolução interna, FSR e precisão da GPU. Salve e reinicie o jogo.': [
        'Check the settings described in each profile. The GTX 1060 performance preset changes internal resolution, FSR and GPU accuracy. Save and restart the game.',
        'Revisa los ajustes de cada perfil. El perfil de rendimiento GTX 1060 cambia la resolución interna, FSR y precisión de GPU. Guarda y reinicia el juego.',
        'Vérifiez les réglages décrits dans chaque profil. Le profil de performances GTX 1060 change la résolution interne, FSR et la précision GPU. Enregistrez et relancez le jeu.'],
    'Depois da captura, encerre o jogo e vá a Logs e diagnóstico → Abrir pasta para enviar.': ['After capture, close the game and go to Logs and diagnostics → Open folder to send.', 'Después de capturar, cierra el juego y ve a Registros y diagnóstico → Abrir carpeta para enviar.', 'Après la capture, fermez le jeu et ouvrez Journaux et diagnostic → Ouvrir le dossier à envoyer.'],
    'Pasta da captura: ': ['Capture folder: ', 'Carpeta de captura: ', 'Dossier de capture : '],
    'A pasta da captura aparecerá aqui após iniciar a sessão de diagnóstico.': ['The capture folder will appear here after starting the diagnostic session.', 'La carpeta aparecerá aquí al iniciar la sesión de diagnóstico.', 'Le dossier apparaîtra ici après le démarrage de la session de diagnostic.'],
    'Abrir pasta para enviar': ['Open folder to send', 'Abrir carpeta para enviar', 'Ouvrir le dossier à envoyer'],
    'Após F11, encerre o jogo. Comprima a pasta session-* inteira e anexe o ZIP à issue. Inclua gtx1060-capture, session.json e os logs.': ['After F11, close the game. Zip the entire session-* folder and attach it to the issue. Include gtx1060-capture, session.json and the logs.', 'Después de F11, cierra el juego. Comprime toda la carpeta session-* y adjunta el ZIP a la issue. Incluye gtx1060-capture, session.json y los registros.', 'Après F11, fermez le jeu. Compressez le dossier session-* entier et joignez le ZIP au ticket. Incluez gtx1060-capture, session.json et les journaux.'],
    'Captura ainda indisponível. Inicie o modo de captura e pressione F11 na cena afetada.': ['Capture is not available yet. Start capture mode and press F11 in the affected scene.', 'La captura aún no está disponible. Inicia el modo de captura y pulsa F11 en la escena afectada.', 'La capture est indisponible. Démarrez le mode capture puis appuyez sur F11 dans la scène affectée.'],
    '3 — Capturar um quadro (F11)': ['3 — Capture one frame (F11)', '3 — Capturar un fotograma (F11)', '3 — Capturer une image (F11)'],
    '4 — Captura + readback linear (F11)': ['4 — Capture + linear readback (F11)', '4 — Captura + readback lineal (F11)', '4 — Capture + readback linéaire (F11)'],
    'Compare os modos 3 e 4 na mesma cena, em sessões separadas. O modo 3 desliga readback linear; o modo 4 liga. Demais ajustes são mantidos. Envie os dois ZIPs.': [
        'Compare modes 3 and 4 at the same scene in separate sessions. Mode 3 disables linear readback; mode 4 enables it. Other settings stay unchanged. Send both ZIPs.',
        'Compara los modos 3 y 4 en la misma escena, en sesiones separadas. El modo 3 desactiva el readback lineal; el modo 4 lo activa. Los demás ajustes se mantienen. Envía ambos ZIP.',
        'Comparez les modes 3 et 4 dans la même scène, dans des sessions distinctes. Le mode 3 désactive le readback linéaire ; le mode 4 l’active. Les autres réglages sont conservés. Envoyez les deux ZIP.'],
    'Na cena afetada, pressione F11 uma vez. A captura pode pausar o jogo e grava até 128 MiB em 13 imagens. Cache e compilação mantêm seus ajustes; inicie outra sessão para repetir.': [
        'At the affected scene, press F11 once. Capture may pause the game and saves up to 128 MiB in 13 images. Cache and compilation keep your settings; start a new session to repeat.',
        'En la escena afectada, pulsa F11 una vez. La captura puede pausar el juego y guarda hasta 128 MiB en 13 imágenes. Conserva los ajustes de caché y compilación; inicia otra sesión para repetir.',
        'Dans la scène affectée, appuyez une fois sur F11. La capture peut suspendre le jeu et enregistre au maximum 128 Mio dans 13 images. Les réglages de cache et compilation restent actifs ; relancez une session pour recommencer.'],
    'Runtime sem captura limitada. Use o pacote de diagnóstico completo.': ['Runtime lacks bounded capture. Use the complete diagnostic package.', 'El runtime no admite captura limitada. Usa el paquete de diagnóstico completo.', 'Le runtime ne prend pas en charge la capture limitée. Utilisez le paquet de diagnostic complet.'],
    'Diagnóstico opcional GTX 1060': ['Optional GTX 1060 diagnostics', 'Diagnóstico opcional GTX 1060', 'Diagnostic GTX 1060 facultatif'],
    '1 — Cache novo por sessão': ['1 — Fresh cache per session', '1 — Caché nueva por sesión', '1 — Nouveau cache par session'],
    '2 — Cache novo + compilação síncrona': ['2 — Fresh cache + synchronous compilation', '2 — Caché nueva + compilación síncrona', '2 — Nouveau cache + compilation synchrone'],
    'Compare a mesma cena: primeiro cache novo, depois compilação síncrona. Cada partida de teste usa cache separado; o cache habitual é preservado. A compilação síncrona pode causar pausas.': [
        'Compare the same scene: fresh cache first, then synchronous compilation. Each test session uses a separate cache; your normal cache is preserved. Synchronous compilation may cause pauses.',
        'Compara la misma escena: primero caché nueva, luego compilación síncrona. Cada sesión de prueba usa una caché separada; conserva la caché habitual. La compilación síncrona puede causar pausas.',
        'Comparez la même scène : nouveau cache, puis compilation synchrone. Chaque session de test utilise un cache séparé ; le cache habituel est conservé. La compilation synchrone peut provoquer des pauses.'],
    'Diagnóstico GTX 1060 inválido.': ['Invalid GTX 1060 diagnostic mode.', 'Diagnóstico GTX 1060 inválido.', 'Mode de diagnostic GTX 1060 invalide.'],
    'Selecione o preset GTX 1060 antes do diagnóstico.': ['Select the GTX 1060 preset before enabling diagnostics.', 'Selecciona el perfil GTX 1060 antes del diagnóstico.', 'Sélectionnez le profil GTX 1060 avant le diagnostic.'],
    'GTX 1060 black texture': ['GTX 1060 black texture', 'GTX 1060 black texture', 'GTX 1060 black texture'],
    'Teste superfícies pretas na GTX 1060. Correção ainda não confirmada.': [
        'Try for black surfaces on the GTX 1060. A fix has not been confirmed yet.',
        'Prueba para superficies negras en la GTX 1060. La solución aún no está confirmada.',
        'À essayer pour les surfaces noires sur GTX 1060. La correction reste à confirmer.'],
    'Ativa leitura precisa da GPU e desliga leitura antecipada. Salve e reinicie o jogo; demais ajustes são preservados.': [
        'Enables precise GPU readbacks and disables early readback. Save and restart the game; other settings are preserved.',
        'Activa la lectura precisa de la GPU y desactiva la lectura anticipada. Guarda y reinicia el juego; conserva los demás ajustes.',
        'Active la lecture précise du GPU et désactive la lecture anticipée. Enregistrez et redémarrez le jeu ; les autres réglages sont conservés.'],
    'Predefinição experimental aplicada. Salve as configurações e reinicie o jogo para comparar.': [
        'Experimental preset applied. Save settings and restart the game to compare.',
        'Preajuste experimental aplicado. Guarda los ajustes y reinicia el juego para comparar.',
        'Préréglage expérimental appliqué. Enregistrez les réglages et redémarrez le jeu pour comparer.'],
    'Predefinições de compatibilidade': ['Compatibility presets', 'Preajustes de compatibilidad', 'Préréglages de compatibilité'],
    'Escolha pelo sintoma e reinicie o jogo para comparar. Resolução, qualidade e controles continuam como você escolheu.': [
        'Choose by symptom and restart the game to compare. Resolution, quality and controls keep your choices.',
        'Elige según el síntoma y reinicia el juego para comparar. La resolución, calidad y controles conservan tus ajustes.',
        'Choisissez selon le symptôme et redémarrez le jeu pour comparer. Résolution, qualité et commandes conservent vos choix.'],
    'Predefinição atual: ': ['Current preset: ', 'Preajuste actual: ', 'Préréglage actuel : '],
    'Personalizado': ['Custom', 'Personalizado', 'Personnalisé'],
    'Experimental': ['Experimental', 'Experimental', 'Expérimental'],
    'Uso normal': ['Normal use', 'Uso normal', 'Utilisation normale'],
    'Selecionado': ['Selected', 'Seleccionado', 'Sélectionné'],
    'Aplicar perfil': ['Apply profile', 'Aplicar perfil', 'Appliquer le profil'],
    'Padrão do port': ['Port defaults', 'Valores predeterminados del port', 'Valeurs par défaut du port'],
    'Comece aqui se o jogo abre e funciona normalmente.': [
        'Start here if the game opens and runs normally.',
        'Empieza aquí si el juego abre y funciona normalmente.',
        'Commencez ici si le jeu démarre et fonctionne normalement.'],
    'Usa os padrões de compatibilidade do port e remove os ajustes de teste.': [
        'Uses the port compatibility defaults and removes test overrides.',
        'Usa los valores de compatibilidad del port y elimina los ajustes de prueba.',
        'Utilise les valeurs de compatibilité du port et supprime les réglages de test.'],
    'AMD: falha ao iniciar': ['AMD: startup crash', 'AMD: fallo al iniciar', 'AMD : plantage au démarrage'],
    'Teste se o jogo fecha logo após clicar em Jogar em uma placa AMD.': [
        'Try if the game closes just after clicking Play on an AMD card.',
        'Prueba si el juego se cierra justo después de pulsar Jugar con una tarjeta AMD.',
        'À essayer si le jeu se ferme juste après avoir cliqué sur Jouer avec une carte AMD.'],
    'Desativa duas opções avançadas para comparar a inicialização. Pode aumentar pausas e alterar efeitos visuais.': [
        'Disables two advanced options to compare startup behavior. May increase pauses and change visual effects.',
        'Desactiva dos opciones avanzadas para comparar el inicio. Puede aumentar pausas y cambiar efectos visuales.',
        'Désactive deux options avancées pour comparer le démarrage. Peut augmenter les pauses et modifier les effets visuels.'],
    'Inicialização conservadora': ['Conservative startup', 'Inicio conservador', 'Démarrage conservateur'],
    'Teste se o perfil AMD não ajudar ou se outra GPU fechar ao iniciar.': [
        'Try if the AMD profile does not help or another GPU crashes at startup.',
        'Prueba si el perfil AMD no ayuda o si otra GPU falla al iniciar.',
        'À essayer si le profil AMD ne suffit pas ou si un autre GPU plante au démarrage.'],
    'Também desliga a compilação compute em segundo plano. Pode causar mais pausas ao carregar conteúdo.': [
        'Also disables background compute compilation. May cause more pauses when loading content.',
        'También desactiva la compilación compute en segundo plano. Puede causar más pausas al cargar contenido.',
        'Désactive aussi la compilation compute en arrière-plan. Peut provoquer davantage de pauses au chargement.'],
    'Perfis de teste não garantem corrigir falhas. Para manter a escolha após fechar o launcher, salve as configurações.': [
        'Test profiles do not guarantee a fix. Save settings to keep your choice after closing the launcher.',
        'Los perfiles de prueba no garantizan corregir fallos. Guarda los ajustes para conservar tu elección al cerrar el launcher.',
        'Les profils de test ne garantissent pas de correction. Enregistrez les paramètres pour conserver votre choix après fermeture.'],
    'Diagnóstico avançado': ['Advanced diagnostics', 'Diagnóstico avanzado', 'Diagnostic avancé'],
    'Mostrar ajustes': ['Show settings', 'Mostrar ajustes', 'Afficher les réglages'],
    'Ocultar ajustes': ['Hide settings', 'Ocultar ajustes', 'Masquer les réglages'],
    'O cache reutiliza programas gráficos compilados pelo driver. Desligar pode aumentar pausas de compilação.': [
        'The cache reuses graphics programs compiled by the driver. Disabling it may increase compilation pauses.',
        'La caché reutiliza programas gráficos compilados por el controlador. Desactivarla puede aumentar las pausas de compilación.',
        'Le cache réutilise les programmes graphiques compilés par le pilote. Le désactiver peut augmenter les pauses de compilation.'],
    'BP06 muda como tarefas de shaders voltam a executar juntas. Desligar pode alterar efeitos visuais.': [
        'BP06 changes how shader tasks resume execution together. Disabling it may change visual effects.',
        'BP06 cambia cómo las tareas de shaders vuelven a ejecutarse juntas. Desactivarlo puede cambiar efectos visuales.',
        'BP06 modifie la façon dont les tâches des shaders reprennent ensemble. Le désactiver peut modifier les effets visuels.'],
    'Desligar a compilação compute em segundo plano pode aumentar pausas ao carregar conteúdo.': [
        'Disabling background compute compilation may increase pauses when loading content.',
        'Desactivar la compilación compute en segundo plano puede aumentar las pausas al cargar contenido.',
        'Désactiver la compilation compute en arrière-plan peut augmenter les pauses au chargement.'],
    'Predefinição de compatibilidade inválida.': ['Invalid compatibility preset.', 'Preajuste de compatibilidad inválido.', 'Préréglage de compatibilité invalide.'],
    'Diagnóstico da GPU': ['GPU diagnostics', 'Diagnóstico de GPU', 'Diagnostic du GPU'],
    'Mude uma opção por vez e reinicie o jogo para comparar. Padrão mantém o comportamento do port.': [
        'Change one option at a time and restart the game to compare. Default keeps the port behavior.',
        'Cambia una opción a la vez y reinicia el juego para comparar. Predeterminado conserva el comportamiento del port.',
        'Modifiez une option à la fois et redémarrez le jeu pour comparer. Par défaut conserve le comportement du port.'],
    'Cache binário de pipelines': ['Pipeline binary cache', 'Caché de binarios de pipelines', 'Cache binaire des pipelines'],
    'Reconvergência de shaders (BP06)': ['Shader reconvergence (BP06)', 'Reconvergencia de shaders (BP06)', 'Reconvergence des shaders (BP06)'],
    'Compilação compute em segundo plano': ['Background compute compilation', 'Compilación compute en segundo plano', 'Compilation compute en arrière-plan'],
    'Padrão': ['Default', 'Predeterminado', 'Par défaut'],
    'Resolução interna (próxima sessão)': ['Internal resolution (next session)', 'Resolución interna (próxima sesión)', 'Résolution interne (prochaine session)'],
    'Automática (FSR / 1080p)': ['Automatic (FSR / 1080p)', 'Automática (FSR / 1080p)', 'Automatique (FSR / 1080p)'],
    '1440p e 4K usam a resolução nativa do jogo até a imagem final. A janela se ajusta ao monitor. Opções menores reduzem a cena e mantêm saída 1080p. Uma escolha manual substitui o preset do FSR.': [
        '1440p and 4K use the game’s native resolution through the final image. The window fits the monitor. Lower options reduce the scene and retain 1080p output. A manual choice overrides the FSR preset.',
        '1440p y 4K usan la resolución nativa del juego hasta la imagen final. La ventana se adapta al monitor. Las opciones menores reducen la escena y mantienen salida 1080p. La selección manual sustituye el preset FSR.',
        '1440p et 4K utilisent la résolution native du jeu jusqu’à l’image finale. La fenêtre s’adapte à l’écran. Les options inférieures réduisent la scène et gardent une sortie 1080p. Un choix manuel remplace le préréglage FSR.'],
    'Proporção e campo de visão': ['Aspect ratio and field of view', 'Proporción y campo de visión', 'Format et champ de vision'],
    'Automática (monitor)': ['Automatic (monitor)', 'Automática (monitor)', 'Automatique (écran)'],
    'Ultrawide amplia o campo de visão horizontal e preserva a proporção da interface. Aplica na próxima sessão.': [
        'Ultrawide expands the horizontal field of view and preserves UI proportions. Applies next session.',
        'Ultrawide amplía el campo de visión horizontal y conserva las proporciones de la interfaz. Se aplica en la próxima sesión.',
        'L’ultralarge élargit le champ de vision horizontal et conserve les proportions de l’interface. Application à la prochaine session.'],
    'FSR temporal': ['Temporal FSR', 'FSR temporal', 'FSR temporel'],
    'Qualidade do FSR temporal': ['Temporal FSR quality', 'Calidad del FSR temporal', 'Qualité du FSR temporel'],
    'Suavização (próxima sessão)': ['Anti-aliasing (next session)', 'Antialiasing (próxima sesión)', 'Anticrénelage (prochaine session)'],
    'Desligado remove o AA temporal nativo e seu jitter. Exige reiniciar, resolução interna 1080p ou maior e FSR temporal desligado. FSR1 e nitidez continuam independentes.': [
        'Off removes native temporal AA and its jitter. Requires restart, internal resolution 1080p or higher, and temporal FSR off. FSR1 and sharpening remain independent.',
        'Desactivado elimina el AA temporal nativo y su jitter. Requiere reiniciar, resolución interna de 1080p o superior y FSR temporal desactivado. FSR1 y nitidez siguen siendo independientes.',
        'Désactivé supprime l’anticrénelage temporel natif et son jitter. Nécessite un redémarrage, une résolution interne de 1080p ou plus et le FSR temporel désactivé. FSR1 et la netteté restent indépendants.'],
    'Qualidade': ['Quality', 'Calidad', 'Qualité'],
    'Equilibrado': ['Balanced', 'Equilibrado', 'Équilibré'],
    'Desempenho': ['Performance', 'Rendimiento', 'Performance'],
    'FSR temporal processa a cena antes da interface. FSR 4 requer modelos locais e GPU compatível; usa FSR 3.1 se indisponível.': [
        'Temporal FSR processes the scene before UI composition. FSR 4 needs local models and a compatible GPU; falls back to FSR 3.1 if unavailable.',
        'FSR temporal procesa la escena antes de la interfaz. FSR 4 requiere modelos locales y una GPU compatible; usa FSR 3.1 si no está disponible.',
        'FSR temporel traite la scène avant l’interface. FSR 4 nécessite des modèles locaux et un GPU compatible ; sinon FSR 3.1 est utilisé.'],
    'Resolução da janela': ['Window resolution', 'Resolución de ventana', 'Résolution de la fenêtre'],
    'Automática (ajustar à tela)': ['Automatic (fit to screen)', 'Automática (ajustar a pantalla)', 'Automatique (adapter à l’écran)'],
    'Aplica na próxima sessão e ajusta à área útil da tela. Tela cheia e sem borda usam a resolução do desktop.': [
        'Applies next session and fits the usable screen. Fullscreen and borderless use the desktop resolution.',
        'Se aplica en la próxima sesión y se ajusta al área útil. Pantalla completa y sin bordes usan la resolución del escritorio.',
        'Application à la prochaine session, adaptée à la zone utile. Plein écran et sans bordure utilisent la résolution du bureau.'],
    'FSR 1 amplia a imagem para saídas maiores, preservando a proporção. Resolução da janela não altera a renderização interna nem garante mais FPS.': [
        'FSR 1 enlarges the image for larger outputs while keeping its shape. Window resolution does not change internal rendering or guarantee more FPS.',
        'FSR 1 amplía la imagen para salidas mayores y conserva su proporción. La resolución de ventana no cambia el renderizado interno ni garantiza más FPS.',
        'FSR 1 agrandit l’image en conservant ses proportions. La résolution de la fenêtre ne change pas le rendu interne et ne garantit pas plus de FPS.'],
    'Início': ['Home', 'Inicio', 'Accueil'],
    'Configurações': ['Settings', 'Configuración', 'Paramètres'],
    'Logs e diagnóstico': ['Logs and diagnostics', 'Registros y diagnóstico', 'Journaux et diagnostic'],
    'Sua próxima aventura': ['Your next adventure', 'Tu próxima aventura', 'Votre prochaine aventure'],
    'Prepare sua experiência': ['Make it your own', 'Prepara tu experiencia', 'Préparez votre expérience'],
    'Acompanhe a sessão': ['Follow your session', 'Sigue la sesión', 'Suivez la session'],
    'Peças de uma grande aventura': ['Pieces of a great adventure', 'Piezas de una gran aventura', 'Les pièces d’une grande aventure'],
    'Pequenas peças. Grandes aventuras.\nPrepare-se para entrar em ação.': ['Small pieces. Big adventures.\nGet ready for action.', 'Pequeñas piezas. Grandes aventuras.\nPrepárate para la acción.', 'Petites pièces. Grandes aventures.\nPréparez-vous à l’action.'],
    'Relíquias que ganham vida': ['Relics come to life', 'Reliquias que cobran vida', 'Des reliques qui prennent vie'],
    'Knack II para PC': ['Knack II for PC', 'Knack II para PC', 'Knack II sur PC'],
    'Demonstração': ['Demo', 'Demostración', 'Démonstration'],
    'Knack II PC — Demonstração': ['Knack II PC — Demo', 'Knack II PC — Demostración', 'Knack II PC — Démonstration'],
    'Sessões sintéticas.\nSem execução do jogo.': ['Simulated sessions.\nNo game execution.', 'Sesiones simuladas.\nSin ejecutar el juego.', 'Sessions simulées.\nSans lancer le jeu.'],
    'Saves preservados.\nInício manual pelo botão Jogar.': ['Saves preserved.\nStart manually with Play.', 'Partidas guardadas.\nInicia con el botón Jugar.', 'Sauvegardes conservées.\nDémarrez avec Jouer.'],
    'CUSA08014\nPerfil 01.01': ['CUSA08014\nProfile 01.01', 'CUSA08014\nPerfil 01.01', 'CUSA08014\nProfil 01.01'],
    'Instalação': ['Installation', 'Instalación', 'Installation'],
    'Selecione seu dump CUSA08014 01.01. Metadados, hashes e módulos devem ser válidos para jogar.': ['Select your CUSA08014 01.01 dump. Files must pass validation before playing.', 'Selecciona tu dump CUSA08014 01.01. Los archivos deben superar la validación para jugar.', 'Sélectionnez votre dump CUSA08014 01.01. Les fichiers doivent être validés pour jouer.'],
    'Selecione seu dump para conferir os arquivos. A demonstração funciona mesmo sem uma instalação.': ['Select your dump to check its files. The demo also works without an installation.', 'Selecciona tu dump para revisar los archivos. La demostración funciona sin instalación.', 'Sélectionnez votre dump pour vérifier les fichiers. La démonstration fonctionne sans installation.'],
    'Pasta do Knack II': ['Knack II folder', 'Carpeta de Knack II', 'Dossier de Knack II'],
    'Pasta do jogo': ['Game folder', 'Carpeta del juego', 'Dossier du jeu'],
    'Selecione a pasta do Knack II (CUSA08014)': ['Select the Knack II folder (CUSA08014)', 'Selecciona la carpeta de Knack II (CUSA08014)', 'Sélectionnez le dossier de Knack II (CUSA08014)'],
    'Escolher pasta': ['Choose folder', 'Elegir carpeta', 'Choisir un dossier'],
    'Validar': ['Validate', 'Validar', 'Valider'],
    'Validando…': ['Validating…', 'Validando…', 'Validation…'],
    'Jogar': ['Play', 'Jugar', 'Jouer'],
    'Jogar · simulação': ['Play · simulation', 'Jugar · simulación', 'Jouer · simulation'],
    'Encerrar sessão': ['End session', 'Terminar sesión', 'Terminer la session'],
    'Encerrar simulação': ['End simulation', 'Terminar simulación', 'Terminer la simulation'],
    'Jogadores: ': ['Players: ', 'Jugadores: ', 'Joueurs : '],
    'Ver logs': ['View logs', 'Ver registros', 'Voir les journaux'],
    'Escolha um ou dois jogadores em Configurações.': ['Choose one or two players in Settings.', 'Elige uno o dos jugadores en Configuración.', 'Choisissez un ou deux joueurs dans les paramètres.'],
    'Nenhum binário do jogo é iniciado. Configurações e sessões desta demonstração são temporárias.': ['No game binary is started. Demo settings and sessions are temporary.', 'No se ejecuta ningún binario del juego. La configuración y las sesiones son temporales.', 'Aucun binaire du jeu n’est lancé. Les paramètres et les sessions sont temporaires.'],
    'Configurações aplicadas na próxima partida. Seus saves permanecem nos perfis de um jogador e cooperativo.': ['Settings apply to the next game. Saves remain in separate single-player and cooperative profiles.', 'Los cambios se aplican a la próxima partida. Las partidas guardadas se conservan en perfiles individuales y cooperativos separados.', 'Les réglages s’appliquent à la prochaine partie. Les sauvegardes restent dans des profils solo et coopératif séparés.'],
    'Ajustes de exemplo. Salvar grava somente na área temporária da demonstração; as preferências reais permanecem intactas.': ['Sample settings. Saving only affects temporary demo data.', 'Ajustes de ejemplo. Guardar solo modifica los datos temporales de la demostración.', 'Réglages de démonstration. L’enregistrement ne concerne que les données temporaires.'],
    'Idioma': ['Language', 'Idioma', 'Langue'],
    'Idioma do launcher e do jogo': ['Launcher and game language', 'Idioma del launcher y del juego', 'Langue du lanceur et du jeu'],
    'O launcher muda agora; o jogo usa o idioma na próxima sessão. Valide a pasta para conferir os idiomas instalados.': ['The launcher changes now; the game changes next session. Validate the folder to check installed languages.', 'El launcher cambia ahora; el juego cambia en la próxima sesión. Valida la carpeta para comprobar los idiomas instalados.', 'Le lanceur change maintenant ; le jeu change à la prochaine session. Validez le dossier pour vérifier les langues installées.'],
    'Jogadores e sessão': ['Players and session', 'Jugadores y sesión', 'Joueurs et session'],
    'Jogadores locais': ['Local players', 'Jugadores locales', 'Joueurs locaux'],
    'Um jogador (padrão)': ['One player (default)', 'Un jugador (predeterminado)', 'Un joueur (par défaut)'],
    'Dois jogadores': ['Two players', 'Dos jugadores', 'Deux joueurs'],
    'Para jogar em cooperativo, selecione dois jogadores e conecte dois controles.': ['For co-op, select two players and connect two controllers.', 'Para jugar en cooperativo, selecciona dos jugadores y conecta dos controles.', 'Pour jouer en coopération, sélectionnez deux joueurs et branchez deux manettes.'],
    'Imagem e apresentação': ['Display and graphics', 'Imagen y presentación', 'Affichage et graphismes'],
    'Modo de janela': ['Display mode', 'Modo de pantalla', 'Mode d’affichage'],
    'Janela': ['Windowed', 'Ventana', 'Fenêtré'],
    'Sem bordas': ['Borderless', 'Sin bordes', 'Sans bordure'],
    'Tela cheia': ['Fullscreen', 'Pantalla completa', 'Plein écran'],
    'Limite de FPS': ['Frame rate limit', 'Límite de FPS', 'Limite d’images par seconde'],
    'Sem limite': ['Unlimited', 'Sin límite', 'Sans limite'],
    'FSR1 espacial': ['Spatial FSR1', 'FSR1 espacial', 'FSR1 spatial'],
    'Nitidez RCAS': ['RCAS sharpening', 'Nitidez RCAS', 'Netteté RCAS'],
    'Intensidade RCAS': ['RCAS strength', 'Intensidad RCAS', 'Intensité RCAS'],
    'Precisão da GPU': ['GPU accuracy', 'Precisión de la GPU', 'Précision du GPU'],
    'Rápido (sem partículas da GPU)': ['Fast (no GPU particles)', 'Rápido (sin partículas de GPU)', 'Rapide (sans particules GPU)'],
    'Intermediário': ['Intermediate', 'Intermedio', 'Intermédiaire'],
    'Preciso (recomendado)': ['Precise (recommended)', 'Preciso (recomendado)', 'Précis (recommandé)'],
    'Preciso prioriza partículas e destroços corretos. Modos mais rápidos podem causar falhas visuais. Aplica na próxima sessão.': ['Precise prioritizes correct particles and debris. Faster modes may cause visual errors. Applies next session.', 'Preciso prioriza partículas y escombros correctos. Los modos más rápidos pueden causar errores visuales. Se aplica en la próxima sesión.', 'Précis privilégie les particules et débris corrects. Les modes rapides peuvent causer des défauts visuels. Appliqué à la prochaine session.'],
    'Leitura antecipada da GPU': ['Early GPU readback', 'Lectura anticipada de la GPU', 'Lecture anticipée du GPU'],
    'Antecipa a leitura dos resultados da GPU. Disponível no modo Preciso; aplica na próxima sessão.': ['Reads GPU results in advance. Available in Precise mode; applies next session.', 'Anticipa la lectura de resultados de la GPU. Disponible en modo Preciso; se aplica en la próxima sesión.', 'Anticipe la lecture des résultats du GPU. Disponible en mode Précis ; appliqué à la prochaine session.'],
    'Controle e teclado': ['Controller and keyboard', 'Control y teclado', 'Manette et clavier'],
    'Controle: detecção automática via SDL.\nTeclado padrão: WASD para mover; setas para câmera.\nEspaço: cruz · Shift esquerdo: círculo · E: quadrado · Q: triângulo\nEnter: Options · I / J / K / L: direcional': ['Controller: automatic detection via SDL.\nKeyboard: WASD to move; arrow keys for the camera.\nSpace: cross · Left Shift: circle · E: square · Q: triangle\nEnter: Options · I / J / K / L: directional pad', 'Control: detección automática mediante SDL.\nTeclado: WASD para moverte; flechas para la cámara.\nEspacio: cruz · Mayús izquierdo: círculo · E: cuadrado · Q: triángulo\nEnter: Options · I / J / K / L: cruceta', 'Manette : détection automatique via SDL.\nClavier : WASD pour bouger ; flèches pour la caméra.\nEspace : croix · Maj gauche : cercle · E : carré · Q : triangle\nEntrée : Options · I / J / K / L : croix directionnelle'],
    'Mouse controla câmera': ['Mouse controls camera', 'Mouse para la cámara', 'Caméra à la souris'],
    'Inverter eixo Y do mouse': ['Invert mouse Y axis', 'Invertir eje Y del mouse', 'Inverser l’axe Y de la souris'],
    'Salvar configurações': ['Save settings', 'Guardar configuración', 'Enregistrer les paramètres'],
    'Salvar na demonstração': ['Save demo settings', 'Guardar en la demostración', 'Enregistrer dans la démonstration'],
    'Sessão: ': ['Session: ', 'Sesión: ', 'Session : '],
    'Saída: ': ['Output: ', 'Salida: ', 'Sortie : '],
    'Copiar logs': ['Copy logs', 'Copiar registros', 'Copier les journaux'],
    'Ver erros': ['View errors', 'Ver errores', 'Voir les erreurs'],
    'Erros registrados': ['Recorded errors', 'Errores registrados', 'Erreurs enregistrées'],
    'Abrir pasta de logs': ['Open logs folder', 'Abrir carpeta de registros', 'Ouvrir le dossier des journaux'],
    'Simular erro': ['Simulate error', 'Simular error', 'Simuler une erreur'],
    'Supervisor': ['Supervisor', 'Supervisor', 'Superviseur'],
    'Jogo': ['Game', 'Juego', 'Jeu'],
    'Guest (sintético)': ['Guest (simulated)', 'Guest (simulado)', 'Guest (simulé)'],
    '500 linhas por painel. Linhas descartadas por excesso de entrada: ': ['500 lines per panel. Lines dropped due to overflow: ', '500 líneas por panel. Líneas descartadas por exceso de entrada: ', '500 lignes par panneau. Lignes supprimées par dépassement : '],
    'Acompanhar últimas linhas': ['Follow latest lines', 'Seguir las últimas líneas', 'Suivre les dernières lignes'],
    'Inicie uma sessão para acompanhar os eventos.': ['Start a session to follow events.', 'Inicia una sesión para seguir los eventos.', 'Démarrez une session pour suivre les événements.'],
    'Inicie uma simulação para acompanhar os eventos.': ['Start a simulation to follow events.', 'Inicia una simulación para seguir los eventos.', 'Démarrez une simulation pour suivre les événements.'],
    'Fechar notificação': ['Dismiss notification', 'Cerrar notificación', 'Fermer la notification'],
    'Ligado': ['On', 'Activado', 'Activé'],
    'Desligado': ['Off', 'Desactivado', 'Désactivé'],
    'Pronto': ['Ready', 'Listo', 'Prêt'],
    'Iniciando sessão': ['Starting session', 'Iniciando sesión', 'Démarrage de la session'],
    'Supervisor ativo': ['Supervisor active', 'Supervisor activo', 'Superviseur actif'],
    'Encerrando sessão': ['Ending session', 'Terminando sesión', 'Fermeture de la session'],
    'Sessão encerrada': ['Session ended', 'Sesión terminada', 'Session terminée'],
    'Falha na sessão': ['Session failed', 'Error en la sesión', 'Échec de la session'],
    'Pronto para simular': ['Ready to simulate', 'Listo para simular', 'Prêt à simuler'],
    'Iniciando simulação': ['Starting simulation', 'Iniciando simulación', 'Démarrage de la simulation'],
    'Simulação em execução': ['Simulation running', 'Simulación en curso', 'Simulation en cours'],
    'Encerrando simulação': ['Ending simulation', 'Terminando simulación', 'Fermeture de la simulation'],
    'Simulação encerrada': ['Simulation ended', 'Simulación terminada', 'Simulation terminée'],
    'Erro na simulação': ['Simulation error', 'Error de simulación', 'Erreur de simulation'],
    'Nenhuma sessão': ['No session', 'Sin sesión', 'Aucune session'],
    'Nenhuma pasta selecionada.': ['No folder selected.', 'Ninguna carpeta seleccionada.', 'Aucun dossier sélectionné.'],
    'Nenhum erro registrado.': ['No errors recorded.', 'No hay errores registrados.', 'Aucune erreur enregistrée.'],
    'Pasta selecionada. Valide a instalação.': ['Folder selected. Validate the installation.', 'Carpeta seleccionada. Valida la instalación.', 'Dossier sélectionné. Validez l’installation.'],
    'Conferindo instalação em segundo plano…': ['Checking installation in the background…', 'Comprobando la instalación en segundo plano…', 'Vérification de l’installation en arrière-plan…'],
    'Instalação válida: metadados, executáveis e módulos conferidos.': ['Installation valid: metadata, executables and modules checked.', 'Instalación válida: metadatos, ejecutables y módulos comprobados.', 'Installation valide : métadonnées, exécutables et modules vérifiés.'],
    'Selecione e valide seu dump. O jogo inicia somente ao clicar em Jogar.': ['Select and validate your dump. The game starts only when you click Play.', 'Selecciona y valida tu dump. El juego solo inicia al pulsar Jugar.', 'Sélectionnez et validez votre dump. Le jeu démarre uniquement avec Jouer.'],
    'Valide a instalação antes de jogar.': ['Validate the installation before playing.', 'Valida la instalación antes de jugar.', 'Validez l’installation avant de jouer.'],
    'Selecione a pasta do seu dump para validar a instalação.': ['Select your dump folder to validate the installation.', 'Selecciona la carpeta de tu dump para validar la instalación.', 'Sélectionnez le dossier de votre dump pour valider l’installation.'],
    'Nenhum idioma compatível encontrado no dump.': ['No supported language found in the dump.', 'No se encontró un idioma compatible en el dump.', 'Aucune langue compatible trouvée dans le dump.'],
    'Idioma indisponível neste dump.': ['Language unavailable in this dump.', 'Idioma no disponible en este dump.', 'Langue indisponible dans ce dump.'],
    'Configurações salvas. Aplicação na próxima partida.': ['Settings saved. Applied next game.', 'Configuración guardada. Se aplicará en la próxima partida.', 'Paramètres enregistrés. Application à la prochaine partie.'],
    'Preparando sessão pelo supervisor oficial. Aguarde os registros de execução.': ['Preparing the session with the official supervisor. Please wait for execution logs.', 'Preparando la sesión con el supervisor oficial. Espera los registros de ejecución.', 'Préparation de la session avec le superviseur officiel. Attendez les journaux d’exécution.'],
    'Encerramento solicitado. Aguardando supervisor e processo do jogo.': ['Shutdown requested. Waiting for the supervisor and game process.', 'Cierre solicitado. Esperando al supervisor y al proceso del juego.', 'Fermeture demandée. Attente du superviseur et du processus du jeu.'],
    'Sessão encerrada por solicitação. Logs e saída preservados.': ['Session ended on request. Logs and output preserved.', 'Sesión terminada a petición. Registros y salida conservados.', 'Session terminée sur demande. Journaux et sortie conservés.'],
    'Logs copiados.': ['Logs copied.', 'Registros copiados.', 'Journaux copiés.'],
    'Modo de demonstração. O jogo real não será iniciado.': ['Demo mode. The real game will not start.', 'Modo de demostración. El juego real no se iniciará.', 'Mode démonstration. Le jeu réel ne sera pas lancé.'],
}
MESSAGES.update({
    'Idiomas sem tradução do launcher usam interface em inglês.': [
        'Languages without a launcher translation use the English interface.',
        'Los idiomas sin traducción del launcher usan la interfaz en inglés.',
        'Les langues sans traduction du launcher utilisent une interface en anglais.'],
    "Captura do port": [
        "Port screenshot",
        "Captura del port",
        "Capture du port"
    ],
    "Arte original": [
        "Original artwork",
        "Arte original",
        "Illustration originale"
    ],
    "Solo": [
        "Solo",
        "Individual",
        "Solo"
    ],
    "Cooperativo": [
        "Co-op",
        "Cooperativo",
        "Coopératif"
    ],
    "Sessões sintéticas. Sem execução do jogo.": [
        "Simulated sessions. No game execution.",
        "Sesiones simuladas. Sin ejecutar el juego.",
        "Sessions simulées. Sans exécution du jeu."
    ],
    "Progresso compartilhado entre solo e cooperativo.": [
        "Progress shared between solo and co-op.",
        "Progreso compartido entre individual y cooperativo.",
        "Progression partagée entre solo et coopération."
    ],
    "Instalação válida": [
        "Installation valid",
        "Instalación válida",
        "Installation valide"
    ],
    "Interface e jogo": [
        "Interface and game",
        "Interfaz y juego",
        "Interface et jeu"
    ],
    "Resolução do desktop": [
        "Desktop resolution",
        "Resolución del escritorio",
        "Résolution du bureau"
    ],
    "Perfil 01.01": [
        "Profile 01.01",
        "Perfil 01.01",
        "Profil 01.01"
    ],
    "Geral": [
        "General",
        "General",
        "Général"
    ],
    "Vídeo": [
        "Video",
        "Vídeo",
        "Vidéo"
    ],
    "Controles": [
        "Controls",
        "Controles",
        "Commandes"
    ],
    "Mouse no analógico direito": [
        "Mouse on right stick",
        "Mouse en palanca derecha",
        "Souris sur le stick droit"
    ],
    "Projeto comunitário não oficial": [
        "Unofficial community project",
        "Proyecto comunitario no oficial",
        "Projet communautaire non officiel"
    ],
    "Direcional": [
        "Directional pad",
        "Cruceta",
        "Croix directionnelle"
    ],
    "Modelo de controle": [
        "Controller model",
        "Modelo de control",
        "Modèle de manette"
    ],
    "Selecione um botão na imagem para entender a função.": [
        "Select a button in the picture to see its function.",
        "Selecciona un botón en la imagen para ver su función.",
        "Sélectionnez un bouton dans l’image pour voir sa fonction."
    ],
    "No controle": [
        "Controller",
        "Control",
        "Manette"
    ],
    "Teclado e mouse": [
        "Keyboard and mouse",
        "Teclado y mouse",
        "Clavier et souris"
    ],
    "Recursos do port": [
        "Port features",
        "Funciones del port",
        "Fonctions du port"
    ],
    "Centro do analógico: clique. Borda: movimento.": [
        "Stick center: press. Edge: movement.",
        "Centro de la palanca: pulsación. Borde: movimiento.",
        "Centre du stick : pression. Bord : mouvement."
    ],
    "No jogo": [
        "In game",
        "En el juego",
        "Dans le jeu"
    ],
    "No teclado": [
        "On keyboard",
        "En el teclado",
        "Au clavier"
    ],
    "Analógico esquerdo move; direito esquiva. O jogo continua usando os símbolos PlayStation.": [
        "Left stick moves; right stick dodges. The game still uses PlayStation symbols.",
        "La palanca izquierda mueve; la derecha esquiva. El juego sigue usando símbolos PlayStation.",
        "Le stick gauche déplace ; le droit esquive. Le jeu utilise toujours les symboles PlayStation."
    ],
    "Todos os comandos": [
        "All controls",
        "Todos los comandos",
        "Toutes les commandes"
    ],
    "Funções podem variar em veículos, transformações e conforme o progresso. Siga as instruções do jogo.": [
        "Functions may vary with vehicles, transformations and progress. Follow the game instructions.",
        "Las funciones varían según vehículos, transformaciones y progreso. Sigue las instrucciones del juego.",
        "Les fonctions varient selon les véhicules, transformations et la progression. Suivez les instructions du jeu."
    ],
    "O mouse, quando ativado, envia o analógico direito: em Knack II pode acionar esquivas, não uma câmera livre.": [
        "When enabled, the mouse sends right-stick input: in Knack II it may trigger dodges, not a free camera.",
        "Al activarse, el mouse envía la palanca derecha: en Knack II puede activar esquivas, no una cámara libre.",
        "La souris activée envoie le stick droit : dans Knack II, elle peut déclencher des esquives, pas une caméra libre."
    ],
    "Teclado e mouse pertencem ao jogador 1. Para cooperativo, selecione dois jogadores e conecte dois controles.": [
        "Keyboard and mouse belong to player 1. For co-op, select two players and connect two controllers.",
        "Teclado y mouse pertenecen al jugador 1. Para cooperativo, elige dos jugadores y conecta dos controles.",
        "Le clavier et la souris sont réservés au joueur 1. Pour coopérer, sélectionnez deux joueurs et branchez deux manettes."
    ],
    "Mover": [
        "Move",
        "Mover",
        "Déplacer"
    ],
    "Esquivar": [
        "Dodge",
        "Esquivar",
        "Esquiver"
    ],
    "Soco": [
        "Punch",
        "Puñetazo",
        "Coup de poing"
    ],
    "Pular": [
        "Jump",
        "Saltar",
        "Sauter"
    ],
    "Chute": [
        "Kick",
        "Patada",
        "Coup de pied"
    ],
    "Ação especial": [
        "Special action",
        "Acción especial",
        "Action spéciale"
    ],
    "Defender": [
        "Block",
        "Defender",
        "Bloquer"
    ],
    "Alterar tamanho": [
        "Change size",
        "Cambiar tamaño",
        "Changer de taille"
    ],
    "Especial da transformação": [
        "Transformation special",
        "Especial de transformación",
        "Spécial de transformation"
    ],
    "Reunir no cooperativo": [
        "Rejoin in co-op",
        "Reunirse en cooperativo",
        "Rejoindre en coopération"
    ],
    "Pausar": [
        "Pause",
        "Pausar",
        "Pause"
    ],
    "Tela de melhorias": [
        "Upgrade screen",
        "Pantalla de mejoras",
        "Écran d’améliorations"
    ],
    "Clique no touchpad": [
        "Touchpad click",
        "Clic del panel táctil",
        "Clic du pavé tactile"
    ],
    "Clique no analógico esquerdo": [
        "Press left stick",
        "Pulsar palanca izquierda",
        "Appuyer sur le stick gauche"
    ],
    "Clique no analógico direito": [
        "Press right stick",
        "Pulsar palanca derecha",
        "Appuyer sur le stick droit"
    ],
    "Mova o analógico para andar na direção desejada.": [
        "Move the stick to walk in the chosen direction.",
        "Mueve la palanca para caminar en la dirección elegida.",
        "Déplacez le stick pour marcher dans la direction souhaitée."
    ],
    "Toque o analógico direito na direção da esquiva. Neste jogo, ele não é um controle de câmera livre.": [
        "Flick the right stick in the dodge direction. In this game it does not control a free camera.",
        "Mueve la palanca derecha hacia la esquiva. En este juego no controla una cámara libre.",
        "Donnez une impulsion au stick droit dans la direction de l’esquive. Il ne contrôle pas une caméra libre."
    ],
    "Pressione para atacar com socos. Golpes e combinações adicionais são apresentados ao longo do jogo.": [
        "Press to punch. More attacks and combinations are introduced as the game progresses.",
        "Pulsa para dar puñetazos. El juego presenta más ataques y combinaciones al avanzar.",
        "Appuyez pour donner un coup de poing. D’autres attaques et combinaisons sont présentées au fil du jeu."
    ],
    "Pressione para saltar. Siga os tutoriais do jogo para saltos e movimentos adicionais.": [
        "Press to jump. Follow game tutorials for additional jumps and movement.",
        "Pulsa para saltar. Sigue los tutoriales para otros saltos y movimientos.",
        "Appuyez pour sauter. Suivez les tutoriels du jeu pour les sauts et mouvements supplémentaires."
    ],
    "Pressione para atacar com um chute. O efeito pode mudar conforme a situação.": [
        "Press to kick. Its effect may vary with the situation.",
        "Pulsa para dar una patada. Su efecto puede variar según la situación.",
        "Appuyez pour donner un coup de pied. L’effet peut varier selon la situation."
    ],
    "Aciona a habilidade especial disponível. A função varia com o progresso, a transformação e as instruções do jogo.": [
        "Uses the available special ability. Its function varies with progress, transformation and game instructions.",
        "Usa la habilidad especial disponible. La función cambia según el progreso, la transformación y las instrucciones.",
        "Active la capacité spéciale disponible. Sa fonction dépend de la progression, de la transformation et des instructions."
    ],
    "Segure para usar a defesa. Alguns ataques e situações exigem outra reação.": [
        "Hold to block. Some attacks and situations require a different response.",
        "Mantén pulsado para defender. Algunos ataques y situaciones requieren otra reacción.",
        "Maintenez pour bloquer. Certaines attaques et situations exigent une autre réaction."
    ],
    "Alterna o tamanho de Knack quando o jogo permite, para atravessar passagens e resolver situações do cenário.": [
        "Changes Knack’s size when allowed, to pass through gaps and solve environmental situations.",
        "Cambia el tamaño de Knack cuando está permitido, para atravesar pasos y resolver situaciones del escenario.",
        "Change la taille de Knack lorsque le jeu le permet, pour franchir des passages et résoudre des situations."
    ],
    "Usa a ação especial de uma transformação que ofereça esse recurso. Não é um golpe universal de todas as formas.": [
        "Uses the special action of a transformation that provides it. It is not a universal attack for every form.",
        "Usa la acción especial de una transformación que la ofrezca. No es un ataque universal de todas las formas.",
        "Utilise l’action spéciale d’une transformation qui la propose. Ce n’est pas une attaque universelle à toutes les formes."
    ],
    "Aciona a função de deslocamento do cooperativo quando disponível. Siga a indicação do jogo para se reunir ao parceiro.": [
        "Uses the co-op warp function when available. Follow the game prompt to rejoin your partner.",
        "Usa el desplazamiento cooperativo cuando esté disponible. Sigue la indicación del juego para reunirte con tu compañero.",
        "Utilise le déplacement coopératif lorsqu’il est disponible. Suivez l’indication du jeu pour rejoindre votre partenaire."
    ],
    "Abre o menu de pausa do próprio jogo. Os ajustes do port usam outro atalho: gatilho esquerdo + clique no analógico direito.": [
        "Opens the game pause menu. Port settings use a different shortcut: left trigger + right-stick press.",
        "Abre el menú de pausa del juego. Los ajustes del port usan otro atajo: gatillo izquierdo + pulsar la palanca derecha.",
        "Ouvre le menu de pause du jeu. Les réglages du port utilisent une autre combinaison : gâchette gauche + pression du stick droit."
    ],
    "O clique do touchpad abre a tela de melhorias. Em controles sem touchpad, View / Share / − emula esse clique. As teclas emulam lados diferentes da superfície.": [
        "A touchpad click opens upgrades. On controllers without a touchpad, View / Share / − emulates it. The keys emulate different sides of the surface.",
        "El clic del panel táctil abre mejoras. Sin panel táctil, View / Share / − emula el clic. Las teclas emulan lados distintos.",
        "Un clic du pavé tactile ouvre les améliorations. Sans pavé tactile, View / Share / − émule ce clic. Les touches émulent différents côtés."
    ],
    "Emula um clique no lado direito do touchpad. Para o lado esquerdo, mantenha o botão superior esquerdo junto. Gestos estão em Recursos do port.": [
        "Emulates a right-side touchpad click. For the left side, also hold the left shoulder button. Gestures are in Port features.",
        "Emula un clic a la derecha del panel táctil. Para la izquierda, mantén también el botón superior izquierdo. Los gestos están en Funciones del port.",
        "Émule un clic à droite du pavé tactile. Pour la gauche, maintenez aussi le bouton supérieur gauche. Les gestes sont dans Fonctions du port."
    ],
    "Envia cima, esquerda, baixo e direita ao jogo. Use nos menus e quando solicitado pelas instruções na tela.": [
        "Sends up, left, down and right to the game. Use in menus and when prompted on screen.",
        "Envía arriba, izquierda, abajo y derecha. Úsalo en menús y cuando lo indique el juego.",
        "Envoie haut, gauche, bas et droite au jeu. Utilisez-le dans les menus et selon les indications à l’écran."
    ],
    "Pressionar o analógico envia L3. Pressionar os dois analógicos juntos alterna o modo sensor herdado do runtime; deixe esse modo desligado no uso normal de Knack II.": [
        "Pressing the stick sends L3. Pressing both sticks toggles the inherited sensor mode; leave it off for normal Knack II play.",
        "Pulsar la palanca envía L3. Pulsar ambas alterna el modo sensor heredado; déjalo desactivado al jugar Knack II normalmente.",
        "Appuyer sur le stick envoie L3. Appuyer sur les deux active le mode capteur hérité ; laissez-le désactivé pour jouer normalement à Knack II."
    ],
    "Pressionar o analógico envia R3. Com o gatilho esquerdo pressionado, abre os ajustes do port.": [
        "Pressing the stick sends R3. With the left trigger held, it opens port settings.",
        "Pulsar la palanca envía R3. Con el gatillo izquierdo pulsado, abre los ajustes del port.",
        "Appuyer sur le stick envoie R3. Avec la gâchette gauche maintenue, cela ouvre les réglages du port."
    ],
    "Controle: detecção automática via SDL.\nTeclado padrão: WASD para mover; setas para esquivar.\nEspaço: cruz · Shift esquerdo: círculo · E: quadrado · Q: triângulo\nEnter: Options · I / J / K / L: direcional": [
        "Controller: automatic detection via SDL.\nKeyboard: WASD to move; arrows to dodge.\nSpace: cross · Left Shift: circle · E: square · Q: triangle\nEnter: Options · I / J / K / L: directional pad",
        "Control: detección automática mediante SDL.\nTeclado: WASD para moverte; flechas para esquivar.\nEspacio: cruz · Mayús izquierdo: círculo · E: cuadrado · Q: triángulo\nEnter: Options · I / J / K / L: cruceta",
        "Manette : détection automatique via SDL.\nClavier : WASD pour bouger ; flèches pour esquiver.\nEspace : croix · Maj gauche : cercle · E : carré · Q : triangle\nEntrée : Options · I / J / K / L : croix directionnelle"
    ],
    "AJUSTES DO PORT\nInsert ou L2 + R3: abrir e fechar o menu de ajustes.\nF10: mostrar ou ocultar FPS.\nEsc: fechar o menu aberto ou liberar o cursor; um clique na janela recaptura o mouse.\n\nCOOPERATIVO\nSelecione dois jogadores no launcher e conecte dois controles. Teclado e mouse ficam no jogador 1. Os dois modos usam o mesmo progresso.\n\nTOUCHPAD SEM SUPERFÍCIE DE TOQUE\nView / Share / Create / −: clique direito do touchpad.\nL1 + esse botão: clique esquerdo.\nR1 + esse botão: dois dedos.\nEsse botão + analógico direito: deslizar.\nTeclado: Tab = lado esquerdo; Backspace = direito; Tab + setas = deslizar; Tab + Backspace = dois dedos.\n\nMOUSE COMO TOUCHPAD\nCom mouse ativo e capturado, segure Ctrl esquerdo ou X1. Esquerdo toca, clica e arrasta; botão do meio toca; direito emula dois dedos.\n\nCOMPATIBILIDADE HERDADA\nL3 + R3 alterna o modo sensor. Deixe desligado para jogar Knack II normalmente: esse modo redireciona os analógicos e o botão inferior. T/G inclinam, setas apontam e Y sacode. Não são golpes adicionais de Knack.\nVibração, luz e sensores dependem do dispositivo e das solicitações do jogo. Remapeamentos personalizados mudam os padrões deste guia.\n\nDIAGNÓSTICO AVANÇADO\nF9 grava entradas somente se a gravação foi habilitada antes da sessão. F11 captura frame somente se essa opção diagnóstica foi habilitada. Não são atalhos ativos por padrão.": [
        "PORT SETTINGS\nInsert or L2 + R3: open and close settings.\nF10: show or hide FPS.\nEsc: close the menu or release the cursor; click the window to capture it again.\n\nCO-OP\nSelect two players and connect two controllers. Keyboard and mouse stay with player 1. Both modes share progress.\n\nTOUCHPAD WITHOUT A TOUCH SURFACE\nView / Share / Create / −: right-side click.\nL1 + this button: left-side click.\nR1 + this button: two fingers.\nThis button + right stick: swipe.\nKeyboard: Tab = left; Backspace = right; Tab + arrows = swipe; Tab + Backspace = two fingers.\n\nMOUSE AS TOUCHPAD\nWith mouse enabled and captured, hold Left Ctrl or X1. Left button touches, clicks and drags; middle touches; right simulates two fingers.\n\nINHERITED COMPATIBILITY\nL3 + R3 toggles sensor mode. Leave it off for normal Knack II play: it redirects sticks and the bottom button. T/G tilt, arrows aim and Y shakes. These are not extra Knack attacks.\nVibration, lights and sensors depend on hardware and game requests. Custom mappings change these defaults.\n\nADVANCED DIAGNOSTICS\nF9 records inputs only if recording was enabled before the session. F11 captures a frame only if that diagnostic option was enabled. Neither is active by default.",
        "AJUSTES DEL PORT\nInsert o L2 + R3: abrir y cerrar ajustes.\nF10: mostrar u ocultar FPS.\nEsc: cerrar el menú o liberar el cursor; pulsa la ventana para capturarlo otra vez.\n\nCOOPERATIVO\nElige dos jugadores y conecta dos controles. Teclado y mouse quedan en el jugador 1. Ambos modos comparten progreso.\n\nPANEL TÁCTIL SIN SUPERFICIE\nView / Share / Create / −: clic derecho.\nL1 + ese botón: clic izquierdo.\nR1 + ese botón: dos dedos.\nEse botón + palanca derecha: deslizar.\nTeclado: Tab = izquierda; Backspace = derecha; Tab + flechas = deslizar; Tab + Backspace = dos dedos.\n\nMOUSE COMO PANEL TÁCTIL\nCon mouse activo y capturado, mantén Ctrl izquierdo o X1. Izquierdo toca, pulsa y arrastra; el central toca; derecho emula dos dedos.\n\nCOMPATIBILIDAD HEREDADA\nL3 + R3 alterna el modo sensor. Déjalo apagado al jugar Knack II normalmente: redirige palancas y botón inferior. T/G inclinan, flechas apuntan e Y sacude. No son ataques adicionales de Knack.\nVibración, luces y sensores dependen del dispositivo y del juego. Los remapeos cambian estos valores.\n\nDIAGNÓSTICO AVANZADO\nF9 graba entradas solo si la grabación se habilitó antes de la sesión. F11 captura un frame solo si se habilitó esa opción diagnóstica. No están activos por defecto.",
        "RÉGLAGES DU PORT\nInsert ou L2 + R3 : ouvrir et fermer les réglages.\nF10 : afficher ou masquer les FPS.\nÉchap : fermer le menu ou libérer le curseur ; cliquez dans la fenêtre pour le capturer à nouveau.\n\nCOOPÉRATION\nSélectionnez deux joueurs et branchez deux manettes. Clavier et souris restent au joueur 1. Les deux modes partagent la progression.\n\nPAVÉ TACTILE ÉMULÉ\nView / Share / Create / − : clic à droite.\nL1 + ce bouton : clic à gauche.\nR1 + ce bouton : deux doigts.\nCe bouton + stick droit : glisser.\nClavier : Tab = gauche ; Retour arrière = droite ; Tab + flèches = glisser ; Tab + Retour arrière = deux doigts.\n\nSOURIS COMME PAVÉ TACTILE\nAvec la souris activée et capturée, maintenez Ctrl gauche ou X1. Le bouton gauche touche, clique et glisse ; le central touche ; le droit simule deux doigts.\n\nCOMPATIBILITÉ HÉRITÉE\nL3 + R3 active le mode capteur. Laissez-le désactivé pour jouer normalement à Knack II : il redirige les sticks et le bouton inférieur. T/G inclinent, les flèches pointent et Y secoue. Ce ne sont pas des attaques supplémentaires de Knack.\nVibrations, lumières et capteurs dépendent du matériel et des demandes du jeu. Les affectations personnalisées changent ces valeurs.\n\nDIAGNOSTIC AVANCÉ\nF9 enregistre les entrées uniquement si cette fonction a été activée avant la session. F11 capture une image uniquement si l’option de diagnostic a été activée. Ces raccourcis sont désactivés par défaut."
    ]
})
MESSAGES.update({
    'Selecione a pasta do Knack II (CUSA08014 ou CUSA07670)': [
        'Select the Knack II folder (CUSA08014 or CUSA07670)',
        'Selecciona la carpeta de Knack II (CUSA08014 o CUSA07670)',
        'Sélectionnez le dossier de Knack II (CUSA08014 ou CUSA07670)'],
    'Selecione seu dump CUSA08014 ou CUSA07670, versão 01.01. Metadados, hashes e módulos devem ser válidos para jogar.': [
        'Select your CUSA08014 or CUSA07670 version 01.01 dump. Files must pass validation before playing.',
        'Selecciona tu dump CUSA08014 o CUSA07670 versión 01.01. Los archivos deben superar la validación para jugar.',
        'Sélectionnez votre dump CUSA08014 ou CUSA07670 version 01.01. Les fichiers doivent être validés pour jouer.'],
})
INDEX = {'en-US': 0, 'en-GB': 0, 'es-419': 1, 'es-ES': 1, 'fr-FR': 2}
MESSAGES.update({
    'Idioma do jogo': ['Game language', 'Idioma del juego', 'Langue du jeu'],
    'Idioma do jogo aplicado na próxima sessão. Valide a pasta para conferir os idiomas instalados.': [
        'Game language applies next session. Validate the folder to check installed languages.',
        'El idioma del juego se aplica en la próxima sesión. Valida la carpeta para comprobar los idiomas instalados.',
        'La langue du jeu sera appliquée à la prochaine session. Validez le dossier pour vérifier les langues installées.'],
    'Use PT / EN / RU no topo para trocar o idioma do launcher.': [
        'Use PT / EN / RU at the top to change the launcher language.',
        'Usa PT / EN / RU arriba para cambiar el idioma del launcher.',
        'Utilisez PT / EN / RU en haut pour changer la langue du lanceur.'],
})


MESSAGES.update({source: list(values[:3]) for source, values in UPDATE_MESSAGES.items()})
MESSAGES['América: CUSA08014 01.00 ou 01.01. Europa: CUSA07670 01.01. Metadados, hashes e módulos devem ser válidos para jogar.'] = [
    'America: CUSA08014 01.00 or 01.01. Europe: CUSA07670 01.01. Metadata, hashes and modules must pass validation before playing.',
    'América: CUSA08014 01.00 o 01.01. Europa: CUSA07670 01.01. Los metadatos, hashes y módulos deben superar la validación para jugar.',
    'Amérique : CUSA08014 01.00 ou 01.01. Europe : CUSA07670 01.01. Les métadonnées, hachages et modules doivent être validés avant de jouer.']
RUSSIAN.update({source: values[3] for source, values in UPDATE_MESSAGES.items()})


def _translate_catalog(locale, source):
    if locale == 'ru-RU':
        if source in RUSSIAN:
            return RUSSIAN[source]
        if source.endswith('.') and source[:-1] in RUSSIAN:
            return RUSSIAN[source[:-1]] + '.'
        return None
    # Game locales do not imply a translated launcher catalog.
    if locale not in ('pt-BR', 'pt-PT') and locale not in INDEX:
        locale = 'en-US'
    values = MESSAGES.get(source)
    if values is not None and locale in INDEX:
        return values[INDEX[locale]]
    if source.endswith('.') and source[:-1] in MESSAGES and locale in INDEX:
        return MESSAGES[source[:-1]][INDEX[locale]] + '.'
    return None


def translate(locale, source):
    translated = _translate_catalog(locale, source)
    if translated is not None:
        return translated
    translated = translate_error(locale, source, _translate_catalog)
    return source if translated is None else translated


@lru_cache(maxsize=512)
def translate_failure(locale, source):
    """Give unknown system/runtime diagnostics a localized heading, preserving bytes."""
    translated = _translate_catalog(locale, source)
    if translated is None:
        translated = translate_error(locale, source, _translate_catalog)
    if translated is not None:
        return translated
    return translate(locale, 'Não foi possível concluir a operação.\nDetalhes técnicos: ' + source)


class Translator(QTranslator):
    def __init__(self, locale):
        super().__init__()
        self.locale = locale

    def isEmpty(self):
        return False

    def translate(self, context, source, disambiguation=None, n=-1):
        return translate(self.locale, source)

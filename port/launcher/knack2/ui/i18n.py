# SPDX-License-Identifier: GPL-2.0-or-later
"""Local UI catalogs. Runtime logs and external diagnostic errors remain verbatim."""
from PySide6.QtCore import QTranslator

# Source Portuguese, followed by English, Latin American Spanish and French.
MESSAGES = {
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
    'Dados isolados F3B.\nInício manual pelo botão Jogar.': ['Separate F3B data.\nStart manually with Play.', 'Datos F3B separados.\nInicia con el botón Jugar.', 'Données F3B séparées.\nDémarrez avec Jouer.'],
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
    'Ajustes aplicados à próxima sessão pelo INI do runtime. Dados e preferências F3B ficam separados do launcher habitual.': ['Settings apply to the next session. F3B data stays separate from your usual launcher.', 'Los cambios se aplican a la próxima sesión. Los datos F3B permanecen separados de tu launcher habitual.', 'Les réglages s’appliquent à la prochaine session. Les données F3B restent séparées de votre lanceur habituel.'],
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
    'Configurações salvas na área isolada F3B. Aplicação na próxima sessão.': ['Settings saved in the separate F3B area. Applied next session.', 'Configuración guardada en el área F3B. Se aplicará en la próxima sesión.', 'Paramètres enregistrés dans l’espace F3B. Application à la prochaine session.'],
    'Preparando sessão pelo supervisor oficial. Aguarde os registros de execução.': ['Preparing the session with the official supervisor. Please wait for execution logs.', 'Preparando la sesión con el supervisor oficial. Espera los registros de ejecución.', 'Préparation de la session avec le superviseur officiel. Attendez les journaux d’exécution.'],
    'Encerramento solicitado. Aguardando supervisor e processo do jogo.': ['Shutdown requested. Waiting for the supervisor and game process.', 'Cierre solicitado. Esperando al supervisor y al proceso del juego.', 'Fermeture demandée. Attente du superviseur et du processus du jeu.'],
    'Sessão encerrada por solicitação. Logs e saída preservados.': ['Session ended on request. Logs and output preserved.', 'Sesión terminada a petición. Registros y salida conservados.', 'Session terminée sur demande. Journaux et sortie conservés.'],
    'Logs copiados.': ['Logs copied.', 'Registros copiados.', 'Journaux copiés.'],
    'Modo de demonstração. O jogo real não será iniciado.': ['Demo mode. The real game will not start.', 'Modo de demostración. El juego real no se iniciará.', 'Mode démonstration. Le jeu réel ne sera pas lancé.'],
}
# Additional English diagnostics used by the real launcher. Raw guest logs stay verbatim.
ENGLISH = {
    'Preparação': 'Preparation',
    'Código': 'Exit code',
    'Logs do supervisor': 'Supervisor logs',
    'Logs do jogo': 'Game logs',
    'aguardando sessão': 'waiting for session',
    'Sessão ambígua; consulte a saída do supervisor.': 'Ambiguous session; check supervisor output.',
    'Build experimental: arquivos conferidos; compatibilidade de jogo não verificada.': 'Experimental build: files checked; gameplay compatibility is unverified.',
    'Não foi possível abrir a pasta de logs.': 'Could not open the logs folder.',
    'Não iniciou: a validação automática encontrou o erro acima.': 'Did not start: automatic validation found the error above.',
    'Não iniciou: nenhuma pasta de dump foi salva. Escolha uma pasta e valide-a primeiro.': 'Did not start: no saved game folder. Choose a folder and validate it first.',
    'Runtime ausente: compile tlg-probe antes de jogar.': 'Runtime missing: build tlg-probe before playing.',
    'a validação automática ainda está em andamento. Aguarde o resultado.': 'automatic validation is still running. Please wait for the result.',
    'já existe uma sessão em andamento.': 'a session is already running.',
    'nenhuma instalação válida foi confirmada.': 'no valid installation has been confirmed.',
    'este dump ainda não tinha uma validação salva antes desta abertura. A validação atual foi concluída; clique em Jogar para confirmar.': 'this dump had no saved validation before opening. Validation is now complete; click Play to continue.',
    'o dump validado anteriormente não é o mesmo título, versão ou pasta da instalação atual. Revise o resultado e clique em Jogar para confirmar.': 'the previously validated dump has a different title, version, or folder. Review the result and click Play to continue.',
}
ENGLISH_PREFIXES = {
    'Processo supervisor encerrou com código de saída ': 'Supervisor exited with code ',
    'Falha ao iniciar processo supervisor: ': 'Failed to start supervisor: ',
    'Falha ao gravar log do supervisor: ': 'Failed to write supervisor log: ',
    'Validação concluída, mas não foi possível registrar o resultado: ': 'Validation complete, but the result could not be saved: ',
    'Não iniciou automaticamente: ': 'Did not start automatically: ',
    'Não iniciou: ': 'Did not start: ',
}

INDEX = {'en-US': 0, 'es-419': 1, 'fr-FR': 2}


def translate(locale, source):
    if locale == 'en-US':
        if source in ENGLISH:
            return ENGLISH[source]
        for prefix, translated in ENGLISH_PREFIXES.items():
            if source.startswith(prefix):
                return translated + translate(locale, source[len(prefix):])
    values = MESSAGES.get(source)
    if values is not None and locale in INDEX:
        return values[INDEX[locale]]
    if source.endswith('.') and source[:-1] in MESSAGES and locale in INDEX:
        return MESSAGES[source[:-1]][INDEX[locale]] + '.'
    return source


class Translator(QTranslator):
    def __init__(self, locale):
        super().__init__()
        self.locale = locale

    def isEmpty(self):
        return False

    def translate(self, context, source, disambiguation=None, n=-1):
        return translate(self.locale, source)

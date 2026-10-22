import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    required property var backend
    property int page: 0
    width: 1180
    height: 810
    minimumWidth: 980
    minimumHeight: 700
    visible: true
    title: backend.isDemo ? qsTr("Knack II PC — Demonstração") : "Knack II PC — F3B"
    color: "#18343f"
    font.family: "Segoe UI"
    font.pixelSize: 14
    palette.window: "#18343f"
    palette.windowText: "#e7f0ef"
    palette.text: "#e7f0ef"
    palette.base: "#102832"
    palette.button: "#284650"
    palette.buttonText: "#e7f0ef"
    palette.highlight: "#efd192"
    palette.highlightedText: "#142e37"
    palette.mid: "#607e87"
    palette.placeholderText: "#90aeb8"
    onClosing: function(event) { event.accepted = backend.requestClose(); }
    Connections {
        target: backend
        ignoreUnknownSignals: true
        function onCloseReady() { window.close(); }
    }

    component Heading: Label {
        font.pixelSize: 27
        font.weight: Font.DemiBold
        color: "#f2f5ef"
        leftPadding: 36
        KnackIcon {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 25; height: 25
            symbol: parent.text === qsTr("Instalação") ? "folder" : parent.text === qsTr("Configurações") ? "settings" : "logs"
        }
    }
    component Hint: Label {
        color: "#afc4c9"
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        lineHeight: 1.25
        Layout.fillWidth: true
    }
    component Section: Rectangle {
        color: "#1d3d48"
        border.color: "#45616a"
        radius: 8
    }
    component SettingLabel: Label {
        font.pixelSize: 15
        Layout.fillWidth: true
    }
    component SectionTitle: RowLayout {
        property alias text: sectionTitle.text
        property string glyph: "relic"
        Layout.fillWidth: true
        spacing: 10
        KnackIcon { symbol: parent.glyph; Layout.preferredWidth: 23; Layout.preferredHeight: 23; ink: "#e5bc78" }
        Label { id: sectionTitle; font.pixelSize: 19; font.weight: Font.DemiBold; color: "#eee5cd"; Layout.fillWidth: true }
    }

    FolderDialog {
        id: folderDialog
        objectName: "folderDialog"
        title: qsTr("Selecione a pasta do Knack II (CUSA08014)")
        onAccepted: backend.selectFolder(selectedFolder.toString())
    }
    Dialog {
        id: errorsDialog
        objectName: "errorsDialog"
        title: qsTr("Erros registrados")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 80, 720)
        height: 350
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            TextArea {
                text: backend.errorText
                textFormat: TextEdit.PlainText
                readOnly: true
                wrapMode: TextEdit.WrapAnywhere
                selectByMouse: true
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 224
            Layout.fillHeight: true
            color: "#102a35"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                RowLayout {
                    Layout.topMargin: 14
                    spacing: 10
                    KnackIcon { width: 27; height: 32; symbol: "relic"; ink: "#f3c677" }
                    Label {
                        text: "KNACK II"
                        font.weight: Font.Bold
                        font.pixelSize: 23
                        font.letterSpacing: 1.5
                        color: "#f3ce8c"
                    }
                }
                Label { text: qsTr("Peças de uma grande aventura"); color: "#9fbec5"; font.pixelSize: 11 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#36505b"; Layout.topMargin: 18; Layout.bottomMargin: 14 }
                Repeater {
                    model: [qsTr("Início"), qsTr("Configurações"), qsTr("Logs e diagnóstico")]
                    delegate: Button {
                        required property int index
                        required property string modelData
                        objectName: "navPage" + index
                        Layout.fillWidth: true
                        implicitHeight: 48
                        text: modelData
                        Accessible.name: text
                        onClicked: window.page = index
                        contentItem: RowLayout {
                            spacing: 10
                            KnackIcon {
                                Layout.leftMargin: 8
                                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                                symbol: ["home", "settings", "logs"][index]
                                ink: window.page === index ? "#f2cf8d" : "#96bac4"
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: window.page === index ? "#f4d296" : "#c0d1d6"
                                font.pixelSize: 13
                                font.family: window.font.family
                                font.weight: window.page === index ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        background: Rectangle {
                            radius: 6
                            color: window.page === index ? "#284652" : parent.hovered ? "#1b3945" : "transparent"
                            border.color: parent.activeFocus ? "#a4e8ef" : "transparent"
                            Rectangle { width: 3; height: 22; radius: 1; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; color: "#edc37d"; visible: window.page === index }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    spacing: 8
                    KnackIcon { symbol: "gamepad"; width: 21; height: 21; ink: "#8cdbdf" }
                    Label { text: backend.isDemo ? qsTr("Demonstração") : qsTr("Knack II para PC"); color: "#efd192"; font.weight: Font.DemiBold }
                }
                Hint { text: backend.isDemo ? qsTr("Sessões sintéticas.\nSem execução do jogo.") : qsTr("Dados isolados F3B.\nInício manual pelo botão Jogar."); font.pixelSize: 12 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#36505b"; Layout.topMargin: 12; Layout.bottomMargin: 8 }
                Label { text: qsTr("CUSA08014\nPerfil 01.01"); color: "#a9c1c9"; font.pixelSize: 12; lineHeight: 1.6 }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 30
            spacing: 20
            RowLayout {
                Layout.fillWidth: true
                Label { text: [qsTr("Sua próxima aventura"), qsTr("Prepare sua experiência"), qsTr("Acompanhe a sessão")][window.page]; color: "#afc4c9" }
                Item { Layout.fillWidth: true }
                Rectangle { width: 8; height: 8; radius: 4; color: backend.state === "FAILED" ? "#ffaaa3" : "#8ed2cc" }
                Label { text: backend.stateLabel; color: "#d8e8e8"; font.pixelSize: 12 }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.page

                ScrollView {
                    id: homeScroll
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: homeScroll.availableWidth
                        spacing: 20
                        Section {
                            Layout.fillWidth: true
                            Layout.preferredHeight: window.height < 760 ? 206 : 252
                            objectName: "knackHero"
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "#2c4950" }
                                GradientStop { position: 1; color: "#153a48" }
                            }
                            border.color: "#6c7668"
                            clip: true
                            Canvas {
                                anchors.fill: parent
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                onPaint: {
                                    const c=getContext("2d"); c.reset();
                                    c.fillStyle="#354f52";
                                    c.beginPath(); c.moveTo(0,0); c.lineTo(width*.59,0); c.lineTo(width*.42,height); c.lineTo(0,height); c.closePath(); c.fill();
                                    c.fillStyle="#3e5654";
                                    c.beginPath(); c.moveTo(0,height); c.lineTo(width*.2,height*.55); c.lineTo(width*.42,height); c.closePath(); c.fill();
                                    c.strokeStyle="#61726a"; c.lineWidth=1;
                                    c.beginPath(); c.moveTo(width*.59,0); c.lineTo(width*.42,height); c.stroke();
                                    c.fillStyle="#eac080"; c.fillRect(0,30,3,46);
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 26
                                spacing: 8
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Label { text: "KNACK II"; font.pixelSize: 48; font.weight: Font.Bold; font.letterSpacing: 2; color: "#f3cf8a"; style: Text.Raised; styleColor: "#493d31" }
                                    Hint { text: qsTr("Pequenas peças. Grandes aventuras.\nPrepare-se para entrar em ação."); font.pixelSize: 16; color: "#d3e0d9" }
                                    Item { Layout.fillHeight: true }
                                    RowLayout {
                                        spacing: 8
                                        KnackIcon { symbol: "relic"; width: 16; height: 19; ink: "#e2b86d" }
                                        Label { text: qsTr("Relíquias que ganham vida"); color: "#c2d3ce"; font.pixelSize: 12 }
                                    }
                                }
                                Relic { objectName: "knackIllustration"; Layout.preferredWidth: 310; Layout.fillHeight: true }
                            }
                        }
                        Heading { text: qsTr("Instalação") }
                        Hint { text: backend.isDemo ? qsTr("Selecione seu dump para conferir os arquivos. A demonstração funciona mesmo sem uma instalação.") : qsTr("Selecione seu dump CUSA08014 01.01. Metadados, hashes e módulos devem ser válidos para jogar.") }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: pathInput
                                objectName: "gamePath"
                                Layout.fillWidth: true
                                implicitHeight: 42
                                placeholderText: qsTr("Pasta do Knack II")
                                text: backend.gameDir
                                selectByMouse: true
                                enabled: backend.editable
                                Accessible.name: qsTr("Pasta do jogo")
                                onEditingFinished: backend.selectFolder(text)
                                onAccepted: { backend.selectFolder(text); backend.validateFolder(); }
                            }
                            ActionButton { text: qsTr("Escolher pasta"); glyph: "folder"; enabled: backend.editable; onClicked: folderDialog.open() }
                            ActionButton { text: backend.validating ? qsTr("Validando…") : qsTr("Validar"); glyph: "check"; enabled: backend.editable; onClicked: { backend.selectFolder(pathInput.text); backend.validateFolder(); } }
                        }
                        Section {
                            Layout.fillWidth: true
                            Layout.preferredHeight: installationInfo.implicitHeight + 32
                            border.color: backend.validationState === "invalid" ? "#b87878" : "#3b5761"
                            ColumnLayout {
                                id: installationInfo
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 7
                                Label { text: backend.installation; textFormat: Text.PlainText; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                                Hint { objectName: "validationMessage"; text: backend.validationText; color: backend.validationState === "invalid" ? "#ffb6ae" : "#afc4c9" }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ActionButton { objectName: "playButton"; text: backend.isDemo ? qsTr("Jogar · simulação") : qsTr("Jogar"); glyph: "play"; primary: true; enabled: backend.canPlay; onClicked: backend.play() }
                            ActionButton { objectName: "stopButton"; text: backend.isDemo ? qsTr("Encerrar simulação") : qsTr("Encerrar sessão"); glyph: "stop"; enabled: backend.canStop; onClicked: backend.stop() }
                            ActionButton { visible: !backend.isDemo; text: backend.isDemo ? "" : qsTr("Jogadores: ") + backend.players; glyph: "players"; onClicked: window.page = 1 }
                            Item { Layout.fillWidth: true }
                            ActionButton { text: qsTr("Ver logs"); glyph: "logs"; onClicked: window.page = 2 }
                        }
                        Hint { text: backend.isDemo ? qsTr("Nenhum binário do jogo é iniciado. Configurações e sessões desta demonstração são temporárias.") : qsTr("Escolha um ou dois jogadores em Configurações."); font.pixelSize: 12 }
                    }
                }

                ColumnLayout {
                    spacing: 12
                    ScrollView {
                    id: settingsScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: settingsScroll.availableWidth
                        spacing: 18
                        Heading { text: qsTr("Configurações") }
                        Hint { text: backend.isDemo ? qsTr("Ajustes de exemplo. Salvar grava somente na área temporária da demonstração; as preferências reais permanecem intactas.") : qsTr("Ajustes aplicados à próxima sessão pelo INI do runtime. Dados e preferências F3B ficam separados do launcher habitual.") }
                        Section {
                            Layout.fillWidth: true
                            Layout.preferredHeight: languageRows.implicitHeight + 40
                            ColumnLayout {
                                id: languageRows
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 12
                                SectionTitle { text: qsTr("Idioma"); glyph: "language" }
                                RowLayout {
                                    SettingLabel { text: qsTr("Idioma do launcher e do jogo") }
                                    OptionSelect {
                                        objectName: "languageSelect"
                                        Layout.preferredWidth: 235
                                        model: backend.languageOptions
                                        textRole: "label"
                                        valueRole: "code"
                                        currentIndex: backend.languageIndex
                                        enabled: backend.editable
                                        Accessible.name: qsTr("Idioma do launcher e do jogo")
                                        onActivated: backend.setLanguage(currentValue)
                                    }
                                }
                                Hint { text: qsTr("O launcher muda agora; o jogo usa o idioma na próxima sessão. Valide a pasta para conferir os idiomas instalados."); font.pixelSize: 12 }
                            }
                        }
                        Section {
                            visible: !backend.isDemo
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? sessionRows.implicitHeight + 40 : 0
                            ColumnLayout {
                                id: sessionRows
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 12
                                SectionTitle { text: qsTr("Jogadores e sessão"); glyph: "players" }
                                RowLayout {
                                    SettingLabel { text: qsTr("Jogadores locais") }
                                    OptionSelect {
                                        objectName: "playerCount"
                                        Layout.preferredWidth: 235
                                        model: [qsTr("Um jogador (padrão)"), qsTr("Dois jogadores")]
                                        currentIndex: backend.isDemo ? 0 : backend.players - 1
                                        enabled: !backend.isDemo && backend.editable
                                        Accessible.name: qsTr("Jogadores locais")
                                        onActivated: backend.setPlayers(currentIndex + 1)
                                    }
                                }
                                Hint { text: qsTr("Para jogar em cooperativo, selecione dois jogadores e conecte dois controles."); font.pixelSize: 12 }
                            }
                        }
                        Section {
                            Layout.fillWidth: true
                            Layout.preferredHeight: graphicsRows.implicitHeight + 40
                            ColumnLayout {
                                id: graphicsRows
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 12
                                SectionTitle { text: qsTr("Imagem e apresentação"); glyph: "monitor" }
                                RowLayout {
                                    Layout.fillWidth: true
                                    SettingLabel { text: qsTr("Modo de janela") }
                                    OptionSelect {
                                        objectName: "displayMode"
                                        Layout.preferredWidth: 210
                                        model: [qsTr("Janela"), qsTr("Sem bordas"), qsTr("Tela cheia")]
                                        currentIndex: ["windowed", "borderless", "fullscreen"].indexOf(backend.settings.display_mode)
                                        Accessible.name: qsTr("Modo de janela")
                                        onActivated: backend.setSetting("display_mode", ["windowed", "borderless", "fullscreen"][currentIndex])
                                    }
                                }
                                RowLayout {
                                    SettingLabel { text: "VSync" }
                                    OptionSwitch { objectName: "vsync"; checked: backend.settings.vsync === "1"; Accessible.name: "VSync"; onToggled: backend.setSetting("vsync", checked) }
                                }
                                RowLayout {
                                    SettingLabel { text: qsTr("Limite de FPS") }
                                    OptionSelect {
                                        objectName: "fpsLimit"
                                        Layout.preferredWidth: 210
                                        model: [qsTr("Sem limite"), "30 FPS", "60 FPS", "120 FPS", "240 FPS"]
                                        currentIndex: ["0", "30", "60", "120", "240"].indexOf(backend.settings.fps_limit)
                                        Accessible.name: qsTr("Limite de FPS")
                                        onActivated: backend.setSetting("fps_limit", [0, 30, 60, 120, 240][currentIndex])
                                    }
                                }
                                RowLayout {
                                    SettingLabel { text: qsTr("FSR1 espacial") }
                                    OptionSwitch { objectName: "fsr1"; checked: backend.settings.fsr1 === "1"; Accessible.name: qsTr("FSR1 espacial"); onToggled: backend.setSetting("fsr1", checked) }
                                }
                                RowLayout {
                                    SettingLabel { text: qsTr("Nitidez RCAS") }
                                    OptionSwitch { objectName: "rcas"; checked: backend.settings.rcas === "1"; Accessible.name: qsTr("Nitidez RCAS"); onToggled: backend.setSetting("rcas", checked) }
                                }
                                RowLayout {
                                    SettingLabel { text: qsTr("Intensidade RCAS") }
                                    Slider {
                                        objectName: "rcasStrength"
                                        Layout.preferredWidth: 165
                                        from: 0; to: 100; stepSize: 1
                                        enabled: backend.settings.rcas === "1"
                                        value: Number(backend.settings.rcas_strength)
                                        Accessible.name: qsTr("Intensidade RCAS")
                                        onMoved: backend.setRcasStrength(value)
                                    }
                                    Label { objectName: "rcasPercent"; text: backend.settings.rcas_strength + "%"; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignRight }
                                }
                            }
                        }
                        Section {
                            Layout.fillWidth: true
                            Layout.preferredHeight: controlsRows.implicitHeight + 40
                            ColumnLayout {
                                id: controlsRows
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 12
                                SectionTitle { text: qsTr("Controle e teclado"); glyph: "gamepad" }
                                Hint { text: qsTr("Controle: detecção automática via SDL.\nTeclado padrão: WASD para mover; setas para câmera.\nEspaço: cruz · Shift esquerdo: círculo · E: quadrado · Q: triângulo\nEnter: Options · I / J / K / L: direcional") }
                                RowLayout {
                                    SettingLabel { text: qsTr("Mouse controla câmera") }
                                    OptionSwitch { objectName: "mouseEnabled"; checked: backend.settings.mouse_enabled === "1"; Accessible.name: qsTr("Mouse controla câmera"); onToggled: backend.setSetting("mouse_enabled", checked) }
                                }
                                RowLayout {
                                    SettingLabel { text: qsTr("Inverter eixo Y do mouse") }
                                    OptionSwitch { checked: backend.settings.mouse_invert_y === "1"; enabled: backend.settings.mouse_enabled === "1"; Accessible.name: qsTr("Inverter eixo Y do mouse"); onToggled: backend.setSetting("mouse_invert_y", checked) }
                                }
                            }
                        }
                    }
                    }
                    ActionButton { objectName: "saveDemoSettings"; text: backend.isDemo ? qsTr("Salvar na demonstração") : qsTr("Salvar configurações"); glyph: "save"; primary: true; onClicked: backend.saveSettings() }
                }

                ColumnLayout {
                    spacing: 16
                    Heading { text: qsTr("Logs e diagnóstico") }
                    Hint { text: qsTr("Sessão: ") + backend.sessionId; font.pixelSize: 13 }
                    Hint { visible: !backend.isDemo; text: backend.isDemo ? "" : backend.sessionDetails; font.pixelSize: 12 }
                    Hint { visible: !backend.isDemo; text: backend.isDemo ? "" : qsTr("Saída: ") + backend.outputText; font.pixelSize: 12 }
                    RowLayout {
                        Layout.fillWidth: true
                        ActionButton { text: qsTr("Copiar logs"); glyph: "copy"; onClicked: backend.copyLogs() }
                        ActionButton { objectName: "showErrors"; text: qsTr("Ver erros"); glyph: "alert"; onClicked: errorsDialog.open() }
                        ActionButton { text: qsTr("Abrir pasta de logs"); glyph: "folder"; visible: !backend.isDemo; onClicked: backend.openLogs() }
                        ActionButton { text: qsTr("Simular erro"); glyph: "alert"; visible: backend.isDemo; danger: true; enabled: backend.state === "RUNNING"; onClicked: backend.simulateError() }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 14
                        LogPanel { Layout.fillWidth: true; Layout.fillHeight: true; title: qsTr("Supervisor"); logModel: backend.supervisorLogs; emptyText: backend.isDemo ? qsTr("Inicie uma simulação para acompanhar os eventos.") : qsTr("Inicie uma sessão para acompanhar os eventos.") }
                        LogPanel { Layout.fillWidth: true; Layout.fillHeight: true; title: backend.isDemo ? qsTr("Guest (sintético)") : qsTr("Jogo"); logModel: backend.guestLogs; emptyText: backend.isDemo ? qsTr("Inicie uma simulação para acompanhar os eventos.") : qsTr("Inicie uma sessão para acompanhar os eventos.") }
                    }
                    Hint { text: qsTr("500 linhas por painel. Linhas descartadas por excesso de entrada: ") + backend.droppedLogs; font.pixelSize: 12 }
                }
            }
            Rectangle {
                objectName: "notificationBanner"
                visible: backend.notification.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Math.max(40, noticeText.implicitHeight + 24) : 0
                color: backend.notificationError ? "#563e44" : "#254650"
                radius: 6
                KnackIcon {
                    anchors.left: parent.left; anchors.leftMargin: 13
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20
                    symbol: backend.notificationError ? "alert" : "relic"
                    ink: backend.notificationError ? "#ffb6ae" : "#dfbc7a"
                }
                Label {
                    id: noticeText
                    objectName: "notificationText"
                    anchors.fill: parent
                    anchors.margins: 12
                    anchors.leftMargin: 45
                    anchors.rightMargin: 46
                    text: backend.notification
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: backend.notificationError ? "#ffcdc6" : "#cee2e5"
                    font.pixelSize: 13
                }
                ToolButton {
                    objectName: "dismissNotification"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 40
                    text: "×"
                    font.pixelSize: 20
                    Accessible.name: qsTr("Fechar notificação")
                    onClicked: backend.dismissNotification()
                }
            }
        }
    }
}

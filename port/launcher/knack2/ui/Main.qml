import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    required property var backend
    property int page: 0
    property int settingsTab: 0
    property bool gpuAdvanced: false
    readonly property string gpuPresetLabel: {
        for (const profile of backend.gpuPresets) {
            if (profile.id === backend.gpuPresetId) return profile.label;
        }
        return qsTr("Personalizado");
    }
    property bool gameplayArt: false
    property bool motionAllowed: typeof launcherMotionAllowed === "undefined" ? false : launcherMotionAllowed
    width: 1180; height: 810; minimumWidth: 980; minimumHeight: 700
    visible: true
    title: backend.isDemo ? qsTr("Knack II PC — Demonstração") : "Knack II PC"
    color: "#232427"; font.family: "Segoe UI"; font.pixelSize: 13
    palette.window: "#232427"; palette.windowText: "#f2f1ee"
    palette.text: "#f2f1ee"; palette.base: "#2d3037"
    palette.button: "#363941"; palette.buttonText: "#f2f1ee"
    palette.highlight: "#d6bb84"; palette.highlightedText: "#262013"
    palette.mid: "#545965"; palette.placeholderText: "#9ca3b2"
    onClosing: function(event) { event.accepted = backend.requestClose(); }
    Connections { target: backend; ignoreUnknownSignals: true; function onCloseReady() { window.close(); } }
    onPageChanged: if (motionAllowed) pageFade.restart()
    NumberAnimation { id: pageFade; target: pages; property: "opacity"; from: 0.5; to: 1; duration: 160 }

    component Heading: Label { font.pixelSize: 26; font.weight: Font.DemiBold }
    component Hint: Label { color: "#afb4c1"; wrapMode: Text.WordWrap; textFormat: Text.PlainText; lineHeight: 1.2; Layout.fillWidth: true; font.pixelSize: 12 }
    component Section: Rectangle { color: "#282a2f"; border.color: "#424650"; radius: 6 }
    component SettingLabel: Label { font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    component SectionTitle: RowLayout {
        property alias text: sectionTitle.text
        property string glyph: "relic"
        Layout.fillWidth: true; spacing: 10
        KnackIcon { symbol: parent.glyph; Layout.preferredWidth: 20; Layout.preferredHeight: 20; ink: "#c4b18b" }
        Label { id: sectionTitle; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
    }
    component SummaryItem: Button {
        id: summary
        property string caption: ""
        property string detail: ""
        Layout.fillWidth: true; implicitHeight: 78; padding: 8
        contentItem: Column {
            spacing: 5
            Label { text: summary.caption; color: "#a7aab5"; font.pixelSize: 11 }
            Label { text: summary.text; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: summary.detail; color: "#9c9fab"; font.pixelSize: 11 }
        }
        background: Rectangle { radius: 5; color: summary.hovered ? "#2f3238" : "transparent"; border.color: summary.activeFocus ? "#d6bb84" : "transparent" }
    }
    FolderDialog {
        id: folderDialog; objectName: "folderDialog"
        title: qsTr("Selecione a pasta do Knack II (CUSA08014 ou CUSA07670)")
        onAccepted: backend.selectFolder(selectedFolder.toString())
    }
    Dialog {
        id: errorsDialog; objectName: "errorsDialog"; title: qsTr("Erros registrados")
        modal: true; anchors.centerIn: parent; width: Math.min(window.width - 80, 720); height: 350
        standardButtons: Dialog.Close
        contentItem: ScrollView { TextArea { text: backend.errorText; textFormat: TextEdit.PlainText; readOnly: true; wrapMode: TextEdit.WrapAnywhere; selectByMouse: true } }
    }
    FileDialog {
        id: baseFileDialog; objectName: "baseFileDialog"
        title: qsTr("Selecione o PKG do jogo-base")
        nameFilters: ["PlayStation 4 (*.pkg)"]
        onAccepted: if (backend.updateManager) backend.updateManager.inspectBase(selectedFile.toString())
    }
    Dialog {
        id: baseConfirm; objectName: "baseConfirm"
        title: qsTr("Extrair jogo na pasta do PKG?")
        modal: true; anchors.centerIn: parent; width: Math.min(window.width - 80, 640)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: backend.updateManager ? backend.updateManager.preview : ""; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
        onAccepted: if (backend.updateManager) backend.updateManager.extractBase()
    }
    FileDialog {
        id: updateFileDialog; objectName: "updateFileDialog"
        title: qsTr("Selecione o PKG de atualização")
        nameFilters: ["PlayStation 4 (*.pkg)"]
        onAccepted: if (backend.updateManager) backend.updateManager.inspect(selectedFile.toString())
    }
    Dialog {
        id: updateConfirm; objectName: "updateConfirm"
        title: qsTr("Instalar atualização do jogo?")
        modal: true; anchors.centerIn: parent; width: Math.min(window.width - 80, 640)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: backend.updateManager ? backend.updateManager.preview : ""; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        onAccepted: if (backend.updateManager) backend.updateManager.install()
    }
    Dialog {
        id: restoreConfirm; objectName: "restoreConfirm"
        title: qsTr("Restaurar arquivos anteriores?")
        modal: true; anchors.centerIn: parent; width: Math.min(window.width - 80, 640)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: qsTr("A atualização será desfeita usando o backup. Arquivos alterados depois da atualização bloqueiam a restauração para preservar suas mudanças."); wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        onAccepted: if (backend.updateManager) backend.updateManager.restore()
    }
    Connections {
        target: backend.updateManager
        function onPreviewReady() { updateConfirm.open(); }
        function onBasePreviewReady() { baseConfirm.open(); }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 62; color: "#232427"; border.color: "#46474c"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 28; anchors.rightMargin: 25; spacing: 24
                Label { text: "Knack II"; font.pixelSize: 19; font.weight: Font.DemiBold; Layout.rightMargin: 10 }
                Repeater {
                    model: [{text: qsTr("Início"), value: 0}, {text: qsTr("Configurações"), value: 1}, {text: qsTr("Instalação"), value: 3}, {text: qsTr("Logs e diagnóstico"), value: 2}]
                    delegate: Button {
                        id: nav
                        required property var modelData
                        objectName: "navPage" + modelData.value
                        Layout.fillHeight: true; text: modelData.text; padding: 4
                        onClicked: window.page = modelData.value
                        contentItem: Text { text: nav.text; font: nav.font; color: window.page === nav.modelData.value ? "#ffffff" : "#b5b7c0"; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle {
                            color: nav.hovered ? "#2b2e34" : "transparent"; border.color: nav.activeFocus ? "#d6bb84" : "transparent"
                            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 3; color: "#d6bb84"; visible: window.page === nav.modelData.value }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                RowLayout {
                    spacing: 2
                    Repeater {
                        model: [{label: "PT", code: "pt-BR", name: "Português"},
                                {label: "EN", code: "en-US", name: "English"},
                                {label: "RU", code: "ru-RU", name: "Русский"}]
                        delegate: Button {
                            id: languageButton
                            required property var modelData
                            objectName: "uiLanguage" + modelData.label
                            implicitWidth: 36; implicitHeight: 34; padding: 5
                            text: modelData.label; checkable: true; autoExclusive: true
                            checked: backend.uiLanguage === modelData.code
                            Accessible.name: modelData.name
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.name
                            onClicked: backend.setUiLanguage(modelData.code)
                            background: Rectangle {
                                radius: 4
                                color: languageButton.checked ? "#45474f" : languageButton.hovered ? "#343941" : "transparent"
                                border.color: languageButton.activeFocus ? "#d6bb84" : languageButton.checked ? "#858e9f" : "transparent"
                            }
                        }
                    }
                }
                Label { objectName: "launcherVersionLabel"; text: launcherVersion === "dev" ? "dev" : "V" + launcherVersion; color: "#b7bdca"; font.pixelSize: 11 }
            }
        }
        StackLayout {
            id: pages
            Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: window.page
            ColumnLayout {
                spacing: 0
                Rectangle {
                    objectName: "knackHero"; Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(390, window.height * .45)
                    color: "#282b32"; clip: true
                    Image { anchors.fill: parent; source: Qt.resolvedUrl(window.gameplayArt ? "art/knack-gameplay.png" : "art/knack-keyart.jpg"); fillMode: Image.PreserveAspectCrop; asynchronous: true; smooth: true }
                    Row {
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16; spacing: 7
                        Repeater {
                            model: ["knack-keyart.jpg", "knack-gameplay.png"]
                            delegate: Button {
                                id: artButton
                                required property int index
                                required property string modelData
                                objectName: "heroArt" + index; width: 68; height: 42
                                Accessible.name: index ? qsTr("Captura do port") : qsTr("Arte original")
                                onClicked: window.gameplayArt = index === 1
                                background: Rectangle {
                                    color: "#202124"; radius: 4; border.width: 2
                                    border.color: window.gameplayArt === (artButton.index === 1) ? "#d6bb84" : "#626670"
                                    Image { anchors.fill: parent; anchors.margins: 3; source: Qt.resolvedUrl("art/" + artButton.modelData); fillMode: Image.PreserveAspectCrop }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 28; spacing: 28
                    ColumnLayout {
                        Layout.preferredWidth: 245; Layout.maximumWidth: 245; Layout.minimumWidth: 210; Layout.alignment: Qt.AlignTop; spacing: 12
                        ActionButton { objectName: "playButton"; Layout.fillWidth: true; implicitHeight: 54; text: backend.isDemo ? qsTr("Jogar · simulação") : qsTr("Jogar"); glyph: "play"; primary: true; enabled: backend.canPlay; onClicked: backend.play() }
                        ActionButton { objectName: "stopButton"; Layout.fillWidth: true; visible: backend.canStop; text: backend.isDemo ? qsTr("Encerrar simulação") : qsTr("Encerrar sessão"); glyph: "stop"; danger: true; enabled: backend.canStop; onClicked: backend.stop() }
                        RowLayout {
                            Layout.fillWidth: true; visible: !backend.isDemo
                            Repeater {
                                model: [1, 2]
                                delegate: Button {
                                    id: playerButton
                                    required property int modelData
                                    objectName: "homePlayers" + modelData; Layout.fillWidth: true
                                    text: modelData === 1 ? qsTr("Solo") : qsTr("Cooperativo"); enabled: backend.editable
                                    onClicked: backend.setPlayers(modelData)
                                    background: Rectangle { radius: 4; color: !backend.isDemo && backend.players === playerButton.modelData ? "#45474f" : "#2a2d33"; border.color: playerButton.activeFocus ? "#d6bb84" : "#555b68" }
                                }
                            }
                        }
                        Hint { text: backend.isDemo ? qsTr("Sessões sintéticas. Sem execução do jogo.") : qsTr("Progresso compartilhado entre solo e cooperativo."); font.pixelSize: 11 }
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#424650" }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: backend.validationState === "valid" && !backend.canStop ? qsTr("Instalação válida") : backend.stateLabel; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true }
                            ActionButton { text: qsTr("Escolher pasta"); glyph: "folder"; enabled: backend.editable; onClicked: { window.page = 3; folderDialog.open(); } }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            SummaryItem { caption: qsTr("Idioma"); text: backend.languageIndex >= 0 ? backend.languageOptions[backend.languageIndex].label : "—"; detail: qsTr("Idioma do jogo"); onClicked: { window.settingsTab = 0; window.page = 1; } }
                            SummaryItem { caption: qsTr("Modo de janela"); text: backend.settings.display_mode === "windowed" ? backend.settings.window_resolution : qsTr("Resolução do desktop"); detail: [qsTr("Janela"), qsTr("Sem bordas"), qsTr("Tela cheia")][["windowed","borderless","fullscreen"].indexOf(backend.settings.display_mode)]; onClicked: { window.settingsTab = 1; window.page = 1; } }
                            SummaryItem { caption: qsTr("Instalação"); text: backend.installationId || "—"; detail: backend.installationVersion; onClicked: window.page = 3 }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#424650" }
                        RowLayout {
                            Layout.fillWidth: true
                            Hint { text: backend.gameDir || qsTr("Nenhuma pasta selecionada."); font.pixelSize: 11; elide: Text.ElideMiddle; wrapMode: Text.NoWrap }
                            ActionButton { text: qsTr("Validar"); enabled: backend.editable; onClicked: { window.page = 3; backend.validateFolder(); } }
                        }
                    }
                }
            }
            RowLayout {
                spacing: 0
                Rectangle {
                    Layout.preferredWidth: 176; Layout.fillHeight: true; color: "#292a2f"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 14; spacing: 6
                        Label { text: qsTr("Configurações"); color: "#aeb5c2"; font.pixelSize: 11; Layout.margins: 10 }
                        Repeater {
                            model: [qsTr("Geral"), qsTr("Vídeo"), qsTr("Controles")]
                            delegate: Button {
                                id: tabButton
                                required property int index
                                required property string modelData
                                objectName: "settingsTab" + index; Layout.fillWidth: true; implicitHeight: 42; text: modelData
                                onClicked: window.settingsTab = index
                                background: Rectangle { radius: 4; color: window.settingsTab === tabButton.index ? "#3b3d45" : "transparent"; border.color: tabButton.activeFocus ? "#d6bb84" : "transparent" }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 25; spacing: 16
                    ScrollView {
                        id: settingsScroll; objectName: "settingsScroll"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
                        ColumnLayout {
                            width: settingsScroll.availableWidth; spacing: 16
                            Section {
 visible: window.settingsTab === 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: languageRows.implicitHeight + 40
                            ColumnLayout {
                                id: languageRows
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 12
                                SectionTitle { text: qsTr("Idioma"); glyph: "language" }
                                RowLayout {
                                    SettingLabel { text: qsTr("Idioma do jogo") }
                                    OptionSelect {
                                        objectName: "languageSelect"
                                        Layout.preferredWidth: 235
                                        model: backend.languageOptions
                                        textRole: "label"
                                        valueRole: "code"
                                        currentIndex: backend.languageIndex
                                        Accessible.name: qsTr("Idioma do jogo")
                                        enabled: backend.editable && count > 0
                                        onActivated: backend.setLanguage(currentValue)
                                    }
                                }
                                Hint { text: qsTr("Idioma do jogo aplicado na próxima sessão. Valide a pasta para conferir os idiomas instalados."); font.pixelSize: 12 }
                                Hint { text: qsTr("Use PT / EN / RU no topo para trocar o idioma do launcher."); font.pixelSize: 12 }
                            }
                        }
                            Section {
                            visible: !backend.isDemo && window.settingsTab === 0
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
                                objectName: "gpuPresetSection"
                                visible: window.settingsTab === 1
                                Layout.fillWidth: true
                                Layout.preferredHeight: profileRows.implicitHeight + 40
                                ColumnLayout {
                                    id: profileRows
                                    anchors.fill: parent; anchors.margins: 20; spacing: 12
                                    SectionTitle { text: qsTr("Predefinições de compatibilidade"); glyph: "monitor" }
                                    Hint { text: qsTr("Confira os ajustes descritos em cada perfil. O preset de desempenho GTX 1060 altera resolução interna, FSR e precisão da GPU. Salve e reinicie o jogo.") }
                                    Label {
                                        objectName: "gpuPresetCurrent"
                                        text: qsTr("Predefinição atual: ") + window.gpuPresetLabel
                                        color: "#d6bb84"; font.pixelSize: 13
                                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                                    }
                                    GridLayout {
                                        Layout.fillWidth: true; columnSpacing: 12; rowSpacing: 12
                                        columns: width >= 700 ? 3 : 1
                                        Repeater {
                                            model: backend.gpuPresets
                                            delegate: Button {
                                                id: profileCard
                                                required property var modelData
                                                objectName: "gpuPreset_" + modelData.id
                                                Layout.fillWidth: true; Layout.fillHeight: true
                                                Layout.preferredWidth: 1
                                                implicitHeight: profileText.implicitHeight + 32
                                                padding: 16; enabled: backend.editable
                                                Accessible.name: modelData.label + ". " + modelData.when + " " + modelData.changes
                                                onClicked: backend.applyGpuPreset(modelData.id)
                                                contentItem: ColumnLayout {
                                                    id: profileText
                                                    spacing: 10
                                                    Label { text: profileCard.modelData.label; font.pixelSize: 16; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                                    Label { text: profileCard.modelData.experimental ? qsTr("Experimental") : qsTr("Uso normal"); color: profileCard.modelData.experimental ? "#d6bb84" : "#afb4c1"; font.pixelSize: 11 }
                                                    Hint { text: profileCard.modelData.when }
                                                    Hint { text: profileCard.modelData.changes }
                                                    Item { Layout.fillHeight: true }
                                                    Label { text: backend.gpuPresetId === profileCard.modelData.id ? qsTr("Selecionado") : qsTr("Aplicar perfil"); color: "#d6bb84"; font.pixelSize: 12 }
                                                }
                                                background: Rectangle {
                                                    radius: 5
                                                    color: backend.gpuPresetId === profileCard.modelData.id ? "#39372f" : profileCard.hovered ? "#32353c" : "#23262c"
                                                    border.width: profileCard.activeFocus ? 2 : 1
                                                    border.color: profileCard.activeFocus || backend.gpuPresetId === profileCard.modelData.id ? "#d6bb84" : "#555b68"
                                                }
                                            }
                                        }
                                    }
                                    ColumnLayout {
                                        objectName: "gtx1060DiagnosticSection"
                                        visible: backend.gpuPresetId === "gtx_1060_black_texture"
                                        Layout.fillWidth: true; spacing: 8
                                        Label {
                                            text: qsTr("Diagnóstico opcional GTX 1060")
                                            color: "#d6bb84"; font.pixelSize: 14
                                            wrapMode: Text.WordWrap; Layout.fillWidth: true
                                        }
                                        OptionSelect {
                                            id: gtx1060Diagnostic
                                            objectName: "gtx1060Diagnostic"
                                            Layout.fillWidth: true
                                            model: [qsTr("Desligado"), qsTr("1 — Cache novo por sessão"),
                                                    qsTr("2 — Cache novo + compilação síncrona"), qsTr("3 — Capturar um quadro (F11)"),
                                                    qsTr("4 — Captura + readback linear (F11)")]
                                            currentIndex: ["off", "cache", "cache_sync", "capture", "capture_linear"].indexOf(backend.gtx1060Diagnostic)
                                            // Retranslation replaces the list; ComboBox resets its index.
                                            onModelChanged: Qt.callLater(function() {
                                                gtx1060Diagnostic.currentIndex = Qt.binding(function() {
                                                    return ["off", "cache", "cache_sync", "capture", "capture_linear"].indexOf(backend.gtx1060Diagnostic);
                                                });
                                            })
                                            enabled: backend.editable
                                            Accessible.name: qsTr("Diagnóstico opcional GTX 1060")
                                            onActivated: backend.setGtx1060Diagnostic(["off", "cache", "cache_sync", "capture", "capture_linear"][currentIndex])
                                        }
                                        Hint { text: ["capture", "capture_linear"].indexOf(backend.gtx1060Diagnostic) >= 0
                                            ? qsTr("Na cena afetada, pressione F11 uma vez. A captura pode pausar o jogo e grava até 128 MiB em 13 imagens. Cache e compilação mantêm seus ajustes; inicie outra sessão para repetir.")
                                            : qsTr("Compare a mesma cena: primeiro cache novo, depois compilação síncrona. Cada partida de teste usa cache separado; o cache habitual é preservado. A compilação síncrona pode causar pausas."); font.pixelSize: 11 }
                                        Hint {
                                            visible: ["capture", "capture_linear"].indexOf(backend.gtx1060Diagnostic) >= 0
                                            text: qsTr("Compare os modos 3 e 4 na mesma cena, em sessões separadas. O modo 3 desliga readback linear; o modo 4 liga. Demais ajustes são mantidos. Envie os dois ZIPs.")
                                            font.pixelSize: 11
                                        }
                                        Hint {
                                            visible: ["capture", "capture_linear"].indexOf(backend.gtx1060Diagnostic) >= 0
                                            text: qsTr("Depois da captura, encerre o jogo e vá a Logs e diagnóstico → Abrir pasta para enviar.")
                                            font.pixelSize: 11
                                        }
                                    }
                                    Hint { text: qsTr("Perfis de teste não garantem corrigir falhas. Para manter a escolha após fechar o launcher, salve as configurações."); font.pixelSize: 11 }
                                }
                            }
                            Section {
 visible: window.settingsTab === 1
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
                                    SettingLabel { text: qsTr("Resolução da janela") }
                                    OptionSelect {
                                        objectName: "windowResolution"
                                        Layout.preferredWidth: 280
                                        model: [qsTr("Automática (ajustar à tela)"), "1280 × 720 (720p)", "1600 × 900 (900p)", "1920 × 1080 (1080p)", "2560 × 1440 (1440p)", "3840 × 2160 (4K)", "2560 × 1080", "3440 × 1440", "3840 × 1600", "5120 × 1440", "5120 × 2160", "7680 × 2160"]
                                        enabled: backend.settings.display_mode === "windowed"
                                        currentIndex: backend.windowResolutions.indexOf(backend.settings.window_resolution)
                                        Accessible.name: qsTr("Resolução da janela")
                                        onActivated: backend.setSetting("window_resolution", backend.windowResolutions[currentIndex])
                                    }
                                }
                                Hint { text: qsTr("Aplica na próxima sessão e ajusta à área útil da tela. Tela cheia e sem borda usam a resolução do desktop."); font.pixelSize: 12 }
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
                                    SettingLabel { text: qsTr("Proporção e campo de visão") }
                                    OptionSelect {
                                        objectName: "aspectRatio"
                                        Layout.preferredWidth: 210
                                        model: [qsTr("Automática (monitor)"), "16:9", "21:9", "32:9"]
                                        currentIndex: ["auto", "16:9", "21:9", "32:9"].indexOf(backend.settings.aspect_ratio)
                                        enabled: backend.editable
                                        Accessible.name: qsTr("Proporção e campo de visão")
                                        onActivated: backend.setSetting("aspect_ratio", ["auto", "16:9", "21:9", "32:9"][currentIndex])
                                    }
                                }
                                Hint { text: qsTr("Ultrawide amplia o campo de visão horizontal e preserva a proporção da interface. Aplica na próxima sessão."); font.pixelSize: 12 }
                                Hint { text: qsTr("FSR 1 amplia a imagem para saídas maiores, preservando a proporção. Resolução da janela não altera a renderização interna nem garante mais FPS."); font.pixelSize: 12 }
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
                                RowLayout {
                                    SettingLabel { text: qsTr("FSR temporal") }
                                    OptionSelect {
                                        objectName: "temporalUpscaler"
                                        enabled: backend.settings.antialiasing !== "off"
                                        Layout.preferredWidth: 280
                                        model: [qsTr("Desligado"), "FSR 3.1", "FSR 4"]
                                        currentIndex: ["off", "fsr3", "fsr4"].indexOf(backend.settings.upscaler)
                                        Accessible.name: qsTr("FSR temporal")
                                        onActivated: backend.setSetting("upscaler", ["off", "fsr3", "fsr4"][currentIndex])
                                    }
                                }
                                RowLayout {
                                    SettingLabel { text: qsTr("Resolução interna (próxima sessão)") }
                                    OptionSelect {
                                        objectName: "internalResolution"
                                        Layout.preferredWidth: 280
                                        model: [qsTr("Automática (FSR / 1080p)"), "1280 × 720 (720p)", "1600 × 900 (900p)", "1920 × 1080 (1080p)", "2560 × 1440 (1440p)", "3840 × 2160 (2160p)"]
                                        currentIndex: backend.internalResolutions.indexOf(backend.settings.internal_resolution)
                                        Accessible.name: qsTr("Resolução interna (próxima sessão)")
                                        onActivated: backend.setSetting("internal_resolution", backend.internalResolutions[currentIndex])
                                    }
                                }
                                Hint { text: qsTr("1440p e 4K usam a resolução nativa do jogo até a imagem final. A janela se ajusta ao monitor. Opções menores reduzem a cena e mantêm saída 1080p. Uma escolha manual substitui o preset do FSR."); font.pixelSize: 12 }
                                RowLayout {
                                    SettingLabel { text: backend.settings.upscaler === "off" ? qsTr("Suavização (próxima sessão)") : qsTr("Qualidade do FSR temporal") }
                                    OptionSelect {
                                        id: temporalPresetControl
                                        objectName: "temporalPreset"
                                        Layout.preferredWidth: 280
                                        enabled: backend.settings.upscaler === "off"
                                            ? ["auto", "1920x1080", "2560x1440", "3840x2160"].indexOf(backend.settings.internal_resolution) >= 0
                                            : backend.settings.internal_resolution === "auto"
                                        model: backend.settings.upscaler === "off"
                                            ? ["Native AA", qsTr("Desligado")]
                                            : ["Native AA", qsTr("Qualidade"), qsTr("Equilibrado"), qsTr("Desempenho")]
                                        function configuredIndex() {
                                            return backend.settings.upscaler === "off"
                                                ? (backend.settings.antialiasing === "off" ? 1 : 0)
                                                : Number(backend.settings.preset)
                                        }
                                        currentIndex: configuredIndex()
                                        // ComboBox resets its index when the translated model changes.
                                        onModelChanged: Qt.callLater(function() {
                                            temporalPresetControl.currentIndex = Qt.binding(function() {
                                                return temporalPresetControl.configuredIndex()
                                            })
                                        })
                                        Accessible.name: backend.settings.upscaler === "off" ? qsTr("Suavização (próxima sessão)") : qsTr("Qualidade do FSR temporal")
                                        onActivated: {
                                            if (backend.settings.upscaler === "off")
                                                backend.setSetting("antialiasing", ["native", "off"][currentIndex])
                                            else
                                                backend.setSetting("preset", currentIndex)
                                        }
                                    }
                                }
                                Hint {
                                    text: backend.settings.upscaler === "off"
                                        ? qsTr("Desligado remove o AA temporal nativo e seu jitter. Exige reiniciar, resolução interna 1080p ou maior e FSR temporal desligado. FSR1 e nitidez continuam independentes.")
                                        : qsTr("FSR temporal processa a cena antes da interface. FSR 4 requer modelos locais e GPU compatível; usa FSR 3.1 se indisponível.")
                                    font.pixelSize: 12
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    SettingLabel { text: qsTr("Precisão da GPU") }
                                    OptionSelect {
                                        objectName: "gpuReadbacks"
                                        Layout.preferredWidth: 280
                                        model: [qsTr("Rápido (sem partículas da GPU)"), qsTr("Intermediário"), qsTr("Preciso (recomendado)")]
                                        currentIndex: Number(backend.settings.gpu_readbacks)
                                        Accessible.name: qsTr("Precisão da GPU")
                                        onActivated: backend.setSetting("gpu_readbacks", ["0", "1", "2"][currentIndex])
                                    }
                                }
                                Hint { text: qsTr("Preciso prioriza partículas e destroços corretos. Modos mais rápidos podem causar falhas visuais. Aplica na próxima sessão."); font.pixelSize: 12 }
                                RowLayout {
                                    Layout.fillWidth: true
                                    SettingLabel { text: qsTr("Leitura antecipada da GPU") }
                                    OptionSwitch {
                                        objectName: "eagerReadback"
                                        enabled: backend.settings.gpu_readbacks === "2"
                                        checked: backend.settings.eager_readback === "1"
                                        Accessible.name: qsTr("Leitura antecipada da GPU")
                                        onToggled: backend.setSetting("eager_readback", checked)
                                    }
                                }
                                Hint { text: qsTr("Antecipa a leitura dos resultados da GPU. Disponível no modo Preciso; aplica na próxima sessão."); font.pixelSize: 12 }
                            }
                        }
                            Section {
                                visible: window.settingsTab === 1
                                Layout.fillWidth: true
                                Layout.preferredHeight: diagnosticRows.implicitHeight + 40
                                ColumnLayout {
                                    id: diagnosticRows
                                    anchors.fill: parent; anchors.margins: 20; spacing: 12
                                    RowLayout {
                                        SectionTitle { text: qsTr("Diagnóstico avançado"); glyph: "monitor" }
                                        Button {
                                            objectName: "gpuAdvancedToggle"
                                            text: window.gpuAdvanced ? qsTr("Ocultar ajustes") : qsTr("Mostrar ajustes")
                                            checkable: true; checked: window.gpuAdvanced
                                            onToggled: window.gpuAdvanced = checked
                                        }
                                    }
                                    Hint { visible: window.gpuAdvanced; text: qsTr("Mude uma opção por vez e reinicie o jogo para comparar. Padrão mantém o comportamento do port.") }
                                    RowLayout {
                                        visible: window.gpuAdvanced
                                        SettingLabel { text: qsTr("Cache binário de pipelines") }
                                        OptionSelect {
                                            objectName: "gpuDiagnosticPipelineBinaries"
                                            Layout.preferredWidth: 210
                                            model: [qsTr("Padrão"), qsTr("Desligado"), qsTr("Ligado")]
                                            currentIndex: ["auto", "0", "1"].indexOf(backend.settings.pipeline_binaries)
                                            enabled: backend.editable
                                            Accessible.name: qsTr("Cache binário de pipelines")
                                            onActivated: backend.setSetting("pipeline_binaries", ["auto", "0", "1"][currentIndex])
                                        }
                                    }
                                    Hint { visible: window.gpuAdvanced; text: qsTr("O cache reutiliza programas gráficos compilados pelo driver. Desligar pode aumentar pausas de compilação."); font.pixelSize: 11 }
                                    RowLayout {
                                        visible: window.gpuAdvanced
                                        SettingLabel { text: qsTr("Reconvergência de shaders (BP06)") }
                                        OptionSelect {
                                            objectName: "gpuDiagnosticBp06"
                                            Layout.preferredWidth: 210
                                            model: [qsTr("Padrão"), qsTr("Desligado"), qsTr("Ligado")]
                                            currentIndex: ["auto", "0", "1"].indexOf(backend.settings.bp06)
                                            enabled: backend.editable
                                            Accessible.name: qsTr("Reconvergência de shaders (BP06)")
                                            onActivated: backend.setSetting("bp06", ["auto", "0", "1"][currentIndex])
                                        }
                                    }
                                    Hint { visible: window.gpuAdvanced; text: qsTr("BP06 muda como tarefas de shaders voltam a executar juntas. Desligar pode alterar efeitos visuais."); font.pixelSize: 11 }
                                    RowLayout {
                                        visible: window.gpuAdvanced
                                        SettingLabel { text: qsTr("Compilação compute em segundo plano") }
                                        OptionSelect {
                                            objectName: "gpuDiagnosticComputeAsync"
                                            Layout.preferredWidth: 210
                                            model: [qsTr("Padrão"), qsTr("Desligado"), qsTr("Ligado")]
                                            currentIndex: ["auto", "0", "1"].indexOf(backend.settings.compute_async_build)
                                            enabled: backend.editable
                                            Accessible.name: qsTr("Compilação compute em segundo plano")
                                            onActivated: backend.setSetting("compute_async_build", ["auto", "0", "1"][currentIndex])
                                        }
                                    }
                                    Hint { visible: window.gpuAdvanced; text: qsTr("Desligar a compilação compute em segundo plano pode aumentar pausas ao carregar conteúdo."); font.pixelSize: 11 }
                                    RowLayout {
                                        visible: window.gpuAdvanced
                                        SettingLabel { text: qsTr("Junção de barreiras de imagem") }
                                        OptionSelect {
                                            id: barrierMergeOption
                                            objectName: "gpuDiagnosticBarrierMerge"
                                            Layout.preferredWidth: 210
                                            model: [qsTr("Padrão"), qsTr("Desligado"), qsTr("Ligado")]
                                            currentIndex: ["auto", "0", "1"].indexOf(backend.settings.barrier_merge)
                                            // A translated model resets ComboBox's index. Restore its binding after reset.
                                            onModelChanged: Qt.callLater(function() {
                                                barrierMergeOption.currentIndex = Qt.binding(function() {
                                                    return ["auto", "0", "1"].indexOf(backend.settings.barrier_merge)
                                                })
                                            })
                                            enabled: backend.editable
                                            Accessible.name: qsTr("Junção de barreiras de imagem")
                                            onActivated: backend.setSetting("barrier_merge", ["auto", "0", "1"][currentIndex])
                                        }
                                    }
                                    Hint { visible: window.gpuAdvanced; text: qsTr("Experimental para RX 580. Padrão deixa desligado. Salve e reinicie o jogo para comparar."); font.pixelSize: 11 }
                                    RowLayout {
                                        visible: window.gpuAdvanced
                                        SettingLabel { text: qsTr("Diagnóstico de barreiras de imagem") }
                                        OptionSelect {
                                            id: barrierTraceOption
                                            objectName: "gpuDiagnosticBarrierTrace"
                                            Layout.preferredWidth: 210
                                            model: [qsTr("Padrão"), qsTr("Desligado"), qsTr("Ligado")]
                                            currentIndex: ["auto", "0", "1"].indexOf(backend.settings.barrier_trace)
                                            onModelChanged: Qt.callLater(function() {
                                                barrierTraceOption.currentIndex = Qt.binding(function() {
                                                    return ["auto", "0", "1"].indexOf(backend.settings.barrier_trace)
                                                })
                                            })
                                            enabled: backend.editable
                                            Accessible.name: qsTr("Diagnóstico de barreiras de imagem")
                                            onActivated: backend.setSetting("barrier_trace", ["auto", "0", "1"][currentIndex])
                                        }
                                    }
                                    Hint { visible: window.gpuAdvanced; text: qsTr("Registra lotes grandes no log, com limite de mensagens. Padrão deixa desligado. Reinicie para aplicar."); font.pixelSize: 11 }
                                }
                            }
                            ControllerGuide { objectName: "controllerGuide"; backend: window.backend; motionAllowed: window.motionAllowed; visible: window.settingsTab === 2; Layout.fillWidth: true; Layout.preferredHeight: 640 }
                            Section {
 visible: window.settingsTab === 2
                            Layout.fillWidth: true
                            Layout.preferredHeight: controlsRows.implicitHeight + 40
                            ColumnLayout {
                                id: controlsRows
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 12
                                SectionTitle { text: qsTr("Controle e teclado"); glyph: "gamepad" }
                                Hint { text: qsTr("Controle: detecção automática via SDL.\nTeclado padrão: WASD para mover; setas para esquivar.\nEspaço: cruz · Shift esquerdo: círculo · E: quadrado · Q: triângulo\nEnter: Options · I / J / K / L: direcional") }
                                RowLayout {
                                    SettingLabel { text: qsTr("Mouse no analógico direito") }
                                    OptionSwitch { objectName: "mouseEnabled"; checked: backend.settings.mouse_enabled === "1"; Accessible.name: qsTr("Mouse no analógico direito"); onToggled: backend.setSetting("mouse_enabled", checked) }
                                }
                                RowLayout {
                                    SettingLabel { text: qsTr("Inverter eixo Y do mouse") }
                                    OptionSwitch { checked: backend.settings.mouse_invert_y === "1"; enabled: backend.settings.mouse_enabled === "1"; Accessible.name: qsTr("Inverter eixo Y do mouse"); onToggled: backend.setSetting("mouse_invert_y", checked) }
                                }
                            }
                        }
                        }
                    }
                    ActionButton { objectName: "saveDemoSettings"; text: backend.isDemo ? qsTr("Salvar na demonstração") : qsTr("Salvar configurações"); glyph: "save"; primary: true; enabled: backend.editable; Layout.alignment: Qt.AlignRight; onClicked: backend.saveSettings() }
                }
            }
            ColumnLayout {
                Layout.margins: 25; spacing: 14
                Heading { text: qsTr("Logs e diagnóstico") }
                Hint { text: qsTr("Sessão: ") + backend.sessionId }
                Hint { visible: !backend.isDemo; text: backend.isDemo ? "" : backend.sessionDetails; font.pixelSize: 12 }
                Hint { visible: !backend.isDemo; text: backend.isDemo ? "" : qsTr("Saída: ") + backend.outputText; font.pixelSize: 11 }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 8
                    visible: !backend.isDemo && (["capture", "capture_linear"].indexOf(backend.gtx1060Diagnostic) >= 0 || backend.captureDirectory !== "")
                    Hint {
                        objectName: "captureLocation"
                        text: backend.isDemo ? "" : backend.captureDirectory !== ""
                              ? qsTr("Pasta da captura: ") + backend.captureDirectory
                              : qsTr("A pasta da captura aparecerá aqui após iniciar a sessão de diagnóstico.")
                        font.pixelSize: 12
                    }
                    ActionButton {
                        objectName: "openCaptureFolder"
                        text: qsTr("Abrir pasta para enviar"); glyph: "folder"
                        enabled: !backend.isDemo && backend.captureDirectory !== ""
                        onClicked: backend.openCaptureFolder()
                    }
                    Hint {
                        text: qsTr("Após F11, encerre o jogo. Comprima a pasta session-* inteira e anexe o ZIP à issue. Inclua gtx1060-capture, session.json e os logs.")
                        font.pixelSize: 11
                    }
                }
                RowLayout {
                    ActionButton { text: qsTr("Copiar logs"); glyph: "copy"; onClicked: backend.copyLogs() }
                    ActionButton { objectName: "showErrors"; text: qsTr("Ver erros"); glyph: "alert"; onClicked: errorsDialog.open() }
                    ActionButton { text: qsTr("Abrir pasta de logs"); glyph: "folder"; visible: !backend.isDemo; onClicked: backend.openLogs() }
                    ActionButton { text: qsTr("Simular erro"); visible: backend.isDemo; enabled: backend.state === "RUNNING"; onClicked: backend.simulateError() }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
                    LogPanel { Layout.fillWidth: true; Layout.fillHeight: true; title: qsTr("Supervisor"); logModel: backend.supervisorLogs; emptyText: qsTr("Inicie uma sessão para acompanhar os eventos.") }
                    LogPanel { Layout.fillWidth: true; Layout.fillHeight: true; title: backend.isDemo ? qsTr("Guest (sintético)") : qsTr("Jogo"); logModel: backend.guestLogs; emptyText: qsTr("Inicie uma sessão para acompanhar os eventos.") }
                }
                Hint { text: qsTr("500 linhas por painel. Linhas descartadas por excesso de entrada: ") + backend.droppedLogs; font.pixelSize: 11 }
            }
            ColumnLayout {
                Layout.margins: 32; spacing: 18
                Heading { text: qsTr("Instalação") }
                Hint { text: qsTr("América: CUSA08014 01.00 ou 01.01. Europa: CUSA07670 01.01. Metadados, hashes e módulos devem ser válidos para jogar.") }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: pathInput; objectName: "gamePath"; Layout.fillWidth: true; implicitHeight: 42
                        placeholderText: qsTr("Pasta do Knack II"); text: backend.gameDir
                        selectByMouse: true; enabled: backend.editable; Accessible.name: qsTr("Pasta do jogo")
                        onEditingFinished: if (text !== backend.gameDir) backend.selectFolder(text)
                        onAccepted: { backend.selectFolder(text); backend.validateFolder(); }
                    }
                    ActionButton { text: qsTr("Escolher pasta"); glyph: "folder"; enabled: backend.editable; onClicked: folderDialog.open() }
                    ActionButton { objectName: "validateButton"; text: backend.validating ? qsTr("Validando…") : qsTr("Validar"); glyph: "check"; enabled: backend.editable; onClicked: { if (pathInput.text !== backend.gameDir) backend.selectFolder(pathInput.text); backend.validateFolder(); } }
                }
                Section {
                    Layout.fillWidth: true; Layout.preferredHeight: 130
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 20; spacing: 12
                        Label { text: backend.installation; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                        Hint { objectName: "validationMessage"; text: backend.validationText; color: backend.validationState === "invalid" ? "#e3aba6" : "#b4c5ae" }
                    }
                }
                RowLayout {
                    visible: !backend.isDemo; Layout.fillWidth: true
                    ActionButton { objectName: "extractBaseButton"; text: qsTr("Extrair jogo-base (.pkg)"); enabled: backend.editable; onClicked: baseFileDialog.open() }
                    ActionButton { objectName: "cancelUpdateButton"; text: qsTr("Cancelar operação PKG"); visible: backend.updateManager ? backend.updateManager.busy : false; onClicked: backend.updateManager.cancel() }
                }
                RowLayout {
                    visible: !backend.isDemo; Layout.fillWidth: true
                    ActionButton { objectName: "installUpdateButton"; text: qsTr("Instalar update (.pkg)"); enabled: backend.editable; onClicked: updateFileDialog.open() }
                    ActionButton { objectName: "restoreUpdateButton"; text: qsTr("Restaurar backup"); enabled: backend.editable; onClicked: restoreConfirm.open() }
                    ActionButton { text: qsTr("Abrir backup"); enabled: backend.editable; onClicked: backend.updateManager.openBackup() }
                }
                Hint { objectName: "updateStatus"; visible: !backend.isDemo; text: backend.updateManager ? backend.updateManager.status : "" }
                ActionButton { text: qsTr("Início"); enabled: backend.canPlay; primary: true; onClicked: window.page = 0 }
                Item { Layout.fillHeight: true }
            }
        }
        Rectangle {
            objectName: "notificationBanner"; visible: backend.notification.length > 0
            Layout.fillWidth: true; Layout.preferredHeight: visible ? Math.max(42, noticeText.implicitHeight + 22) : 0
            color: backend.notificationError ? "#443334" : "#303640"
            Label {
                id: noticeText; objectName: "notificationText"
                anchors.left: parent.left; anchors.right: dismiss.left; anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 22; anchors.rightMargin: 16
                text: backend.notification; textFormat: Text.PlainText; wrapMode: Text.WordWrap
                color: backend.notificationError ? "#f0c2b9" : "#d6deeb"; font.pixelSize: 12
            }
            ToolButton { id: dismiss; objectName: "dismissNotification"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 42; height: 40; text: "×"; Accessible.name: qsTr("Fechar notificação"); onClicked: backend.dismissNotification() }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 32; color: "#292a2e"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                Rectangle { width: 5; height: 5; radius: 3; color: backend.state === "FAILED" ? "#ce938b" : "#a8c2a0" }
                Label { text: backend.stateLabel; font.pixelSize: 10; color: "#aeb5c2" }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("Projeto comunitário não oficial") + " · Knack II © Sony Interactive Entertainment"; font.pixelSize: 10; color: "#9ea8bb" }
            }
        }
    }
}

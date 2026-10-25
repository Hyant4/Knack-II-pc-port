import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ControllerGuideData.js" as Guide

Item {
    id: root
    required property var backend
    property bool motionAllowed: true
    property int guidePage: 0
    property int selected: 2
    property string family: ["ps", "xbox", "nintendo"][Math.max(0, familySelect.currentIndex)] || "ps"
    property var photo: Guide.photos[family] || Guide.photos.ps
    property var command: Guide.commands[selected]
    property var view: photo.view.split(" ").map(Number)
    implicitHeight: 640
    function padName(entry) {
        return entry.id === "dpad" ? qsTr("Direcional") :
            Guide.families[family][entry.label] || entry.ps;
    }
    function selectCommand(id) {
        for (let i = 0; i < Guide.commands.length; ++i)
            if (Guide.commands[i].id === id) { selected = i; return; }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Controles"); font.pixelSize: 26; font.weight: Font.DemiBold; Layout.fillWidth: true }
            OptionSelect { id: familySelect; objectName: "guideFamily"; Layout.preferredWidth: 190; model: ["PlayStation", "Xbox", "Nintendo / Switch"]; Accessible.name: qsTr("Modelo de controle") }
        }
        Label { text: qsTr("Selecione um botão na imagem para entender a função."); color: "#afb4c1"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            spacing: 10
            Repeater {
                model: [qsTr("No controle"), qsTr("Teclado e mouse"), qsTr("Recursos do port")]
                delegate: Button {
                    required property int index
                    required property string modelData
                    objectName: "guideTab" + index
                    text: modelData; padding: 10
                    onClicked: root.guidePage = index
                    background: Rectangle { radius: 4; color: root.guidePage === index ? "#3b3d45" : "transparent"; border.color: parent.activeFocus ? "#d6bb84" : "transparent" }
                }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.guidePage
            ColumnLayout {
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 320
                    spacing: 18
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 540
                        radius: 6; color: "#1e2024"; border.color: "#484b54"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 8
                            Label { text: root.photo.name; color: "#b5bdcc"; font.pixelSize: 11 }
                            Item {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                Item {
                                    id: drawing
                                    property real factor: Math.min(parent.width / root.view[2], parent.height / root.view[3])
                                    width: root.view[2] * factor; height: root.view[3] * factor
                                    anchors.centerIn: parent
                                    clip: true
                                    Image {
                                        objectName: "controllerPhoto"
                                        x: -root.view[0] * drawing.factor; y: -root.view[1] * drawing.factor
                                        width: root.photo.width * drawing.factor; height: root.photo.height * drawing.factor
                                        source: Qt.resolvedUrl("art/" + root.photo.file)
                                        fillMode: Image.Stretch; smooth: true; asynchronous: true
                                    }
                                    Repeater {
                                        model: Object.keys(root.photo.points)
                                        delegate: Button {
                                            required property string modelData
                                            property var point: root.photo.points[modelData] || [0, 0, 0]
                                            objectName: "guideControl_" + modelData
                                            x: (point[0] - point[2] - root.view[0]) * drawing.factor
                                            y: (point[1] - (point.length === 4 ? point[3] : point[2]) - root.view[1]) * drawing.factor
                                            width: point[2] * 2 * drawing.factor
                                            height: (point.length === 4 ? point[3] : point[2]) * 2 * drawing.factor
                                            Accessible.name: modelData
                                            onClicked: root.selectCommand(modelData)
                                            background: Rectangle {
                                                radius: width / 2
                                                color: parent.hovered ? "#20c29a4a" : "transparent"
                                                border.color: root.command.id === parent.modelData || parent.activeFocus ? "#d6bb84" : "transparent"
                                                border.width: 2
                                            }
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Repeater {
                                    model: ["l2", "l1", "r1", "r2"]
                                    delegate: ActionButton {
                                        required property string modelData
                                        objectName: "guideShoulder_" + modelData
                                        Layout.fillWidth: true; implicitWidth: 55; implicitHeight: 32
                                        text: Guide.families[root.family][modelData]
                                        onClicked: root.selectCommand(modelData)
                                    }
                                }
                            }
                            Label { text: qsTr("Centro do analógico: clique. Borda: movimento."); color: "#a7b0c0"; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                    Rectangle {
                        Layout.preferredWidth: Math.max(245, root.width * .34)
                        Layout.fillHeight: true
                        radius: 6; color: "#2b2e34"; border.color: "#5a5c64"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 20; spacing: 12
                            Label { text: root.padName(root.command); font.pixelSize: 16; color: "#e7d5ae" }
                            Label { objectName: "guideFunction"; text: qsTr(root.command.title); font.pixelSize: 21; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Label { objectName: "guideDescription"; text: qsTr(root.command.description); color: "#c0c3ce"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
                            Item { Layout.fillHeight: true }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#48494d" }
                            Label { text: qsTr("No jogo") + ": " + root.command.ps; color: "#bbc5d4"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: qsTr("No teclado") + ": " + root.command.keys.join(" / "); color: "#e1e5ed"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
                Label { text: qsTr("Analógico esquerdo move; direito esquiva. O jogo continua usando os símbolos PlayStation."); color: "#b1b7c5"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                OptionSelect {
                    objectName: "guideAllCommands"
                    Layout.fillWidth: true
                    model: Guide.commands.map(function(entry) { return root.padName(entry) + " — " + qsTr(entry.title); })
                    currentIndex: root.selected
                    Accessible.name: qsTr("Todos os comandos")
                    onActivated: root.selected = currentIndex
                }
                Label { text: qsTr("Funções podem variar em veículos, transformações e conforme o progresso. Siga as instruções do jogo."); color: "#9fa9ba"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Item { Layout.fillHeight: true }
            }
            ScrollView {
                id: keyboardScroll
                clip: true; contentWidth: availableWidth
                ColumnLayout {
                    width: keyboardScroll.availableWidth; spacing: 1
                    Label { Layout.fillWidth: true; text: qsTr("O mouse, quando ativado, envia o analógico direito: em Knack II pode acionar esquivas, não uma câmera livre."); wrapMode: Text.WordWrap; color: "#d6bb84"; font.pixelSize: 12; bottomPadding: 14 }
                    Repeater {
                        model: Guide.commands
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true; implicitHeight: 42; color: index % 2 ? "#292c32" : "#25282d"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                                Label { text: qsTr(modelData.title); Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                                Label { text: modelData.keys.join(" / "); Layout.preferredWidth: 180; color: "#d6bb84"; font.pixelSize: 12; wrapMode: Text.WordWrap }
                                Label { text: modelData.ps; Layout.preferredWidth: 110; color: "#9fa9ba"; font.pixelSize: 11 }
                            }
                        }
                    }
                    Label { text: qsTr("Teclado e mouse pertencem ao jogador 1. Para cooperativo, selecione dois jogadores e conecte dois controles."); Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#adb5c4"; topPadding: 14; font.pixelSize: 12 }
                }
            }
            ScrollView {
                contentWidth: availableWidth; clip: true
                TextArea {
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.WordWrap
                    textFormat: TextEdit.PlainText; font.pixelSize: 13; color: "#d0d6e0"
                    text: qsTr("AJUSTES DO PORT\nInsert ou L2 + R3: abrir e fechar o menu de ajustes.\nF10: mostrar ou ocultar FPS.\nEsc: fechar o menu aberto ou liberar o cursor; um clique na janela recaptura o mouse.\n\nCOOPERATIVO\nSelecione dois jogadores no launcher e conecte dois controles. Teclado e mouse ficam no jogador 1. Os dois modos usam o mesmo progresso.\n\nTOUCHPAD SEM SUPERFÍCIE DE TOQUE\nView / Share / Create / −: clique direito do touchpad.\nL1 + esse botão: clique esquerdo.\nR1 + esse botão: dois dedos.\nEsse botão + analógico direito: deslizar.\nTeclado: Tab = lado esquerdo; Backspace = direito; Tab + setas = deslizar; Tab + Backspace = dois dedos.\n\nMOUSE COMO TOUCHPAD\nCom mouse ativo e capturado, segure Ctrl esquerdo ou X1. Esquerdo toca, clica e arrasta; botão do meio toca; direito emula dois dedos.\n\nCOMPATIBILIDADE HERDADA\nL3 + R3 alterna o modo sensor. Deixe desligado para jogar Knack II normalmente: esse modo redireciona os analógicos e o botão inferior. T/G inclinam, setas apontam e Y sacode. Não são golpes adicionais de Knack.\nVibração, luz e sensores dependem do dispositivo e das solicitações do jogo. Remapeamentos personalizados mudam os padrões deste guia.\n\nDIAGNÓSTICO AVANÇADO\nF9 grava entradas somente se a gravação foi habilitada antes da sessão. F11 captura frame somente se essa opção diagnóstica foi habilitada. Não são atalhos ativos por padrão.")
                    background: Rectangle { color: "#25282e"; radius: 5 }
                }
            }
        }
    }
}

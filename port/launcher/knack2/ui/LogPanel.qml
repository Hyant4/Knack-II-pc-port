import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    required property var logModel
    required property string title
    property string emptyText: qsTr("Inicie uma simulação para acompanhar os eventos.")
    color: "#102832"
    radius: 8
    border.color: "#39515a"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            KnackIcon { symbol: panel.title === qsTr("Supervisor") ? "monitor" : "gamepad"; Layout.preferredWidth: 21; Layout.preferredHeight: 21; ink: "#dfbc7c" }
            Label { text: panel.title; font.pixelSize: 17; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Label { text: view.count + " / 500"; color: "#afc4c9"; font.pixelSize: 12 }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#39515a" }
        ListView {
            id: view
            objectName: panel.title === qsTr("Supervisor") ? "supervisorView" : "guestView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: panel.logModel
            property bool followTail: true
            onMovementStarted: followTail = false
            onCountChanged: if (followTail) Qt.callLater(positionViewAtEnd)
            ScrollBar.vertical: ScrollBar { }
            delegate: Text {
                required property string line
                required property bool isError
                width: view.width - 14
                text: line
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                color: isError ? "#ffaaa3" : "#d3e3e5"
                font.family: "Consolas"
                font.pixelSize: 12
                lineHeight: 1.25
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 20
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: view.count === 0
                text: panel.emptyText
                color: "#afc4c9"
                KnackIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.top; anchors.bottomMargin: 16
                    width: 36; height: 36
                    symbol: panel.title === qsTr("Supervisor") ? "logs" : "relic"
                    ink: "#688e99"
                }
            }
        }
        ActionButton {
            text: qsTr("Acompanhar últimas linhas")
            Layout.fillWidth: true
            onClicked: { view.followTail = true; view.positionViewAtEnd(); }
        }
    }
}

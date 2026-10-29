import QtQuick
import QtQuick.Controls

Switch {
    id: control
    implicitWidth: 122
    implicitHeight: 40
    spacing: 10
    indicator: Rectangle {
        x: 0
        y: (control.height - height) / 2
        width: 46
        height: 25
        radius: 13
        color: control.checked ? "#d6bb84" : "#3d424e"
        border.color: control.activeFocus ? "#d6bb84" : control.checked ? "#d6bb84" : "#858e9f"
        border.width: control.activeFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.45
        Rectangle {
            x: control.checked ? 24 : 4
            y: 4
            width: 17
            height: 17
            radius: 9
            color: control.checked ? "#262013" : "#d7dfed"
        }
    }
    contentItem: Text {
        leftPadding: 56
        text: control.checked ? qsTr("Ligado") : qsTr("Desligado")
        color: control.enabled ? "#d7dfed" : "#939dab"
        font: control.font
        verticalAlignment: Text.AlignVCenter
    }
}

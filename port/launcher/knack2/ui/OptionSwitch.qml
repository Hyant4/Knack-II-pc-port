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
        color: control.checked ? "#efd192" : "#355661"
        border.color: control.activeFocus ? "#a4e8ef" : control.checked ? "#efd192" : "#8aabb4"
        border.width: control.activeFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.45
        Rectangle {
            x: control.checked ? 24 : 4
            y: 4
            width: 17
            height: 17
            radius: 9
            color: control.checked ? "#18343f" : "#d5e4e7"
        }
    }
    contentItem: Text {
        leftPadding: 56
        text: control.checked ? qsTr("Ligado") : qsTr("Desligado")
        color: control.enabled ? "#d5e4e7" : "#99b1b9"
        font: control.font
        verticalAlignment: Text.AlignVCenter
    }
}

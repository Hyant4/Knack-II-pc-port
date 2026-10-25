import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property bool danger: false
    property string glyph: ""
    implicitHeight: 42
    implicitWidth: Math.max(112, contentItem.implicitWidth + 30)
    padding: 12
    font.pixelSize: 14
    font.weight: Font.DemiBold
    Accessible.name: text
    contentItem: Item {
        implicitWidth: content.width
        implicitHeight: content.height
        Row {
            id: content
            anchors.centerIn: parent
            spacing: 8
            KnackIcon {
                visible: control.glyph.length > 0
                symbol: control.glyph
                width: 18; height: 18
                anchors.verticalCenter: parent.verticalCenter
                ink: !control.enabled ? "#747c8a" : control.primary ? "#262013" : control.danger ? "#e3aba6" : "#d6bb84"
            }
            Text {
                text: control.text
                font: control.font
                color: !control.enabled ? "#8d95a4" : control.primary ? "#262013" : "#f2f1ee"
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    background: Rectangle {
        radius: 6
        color: !control.enabled ? "#30343c" : control.primary ? (control.down ? "#b59a66" : "#d6bb84") : control.hovered ? "#414751" : "#343941"
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#d6bb84" : control.danger ? "#b58078" :
                      control.primary ? "#d6bb84" : "#565e6d"
    }
}

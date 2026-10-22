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
                ink: !control.enabled ? "#738d94" : control.primary ? "#263b40" : control.danger ? "#efaaa0" : "#edc887"
            }
            Text {
                text: control.text
                font: control.font
                color: !control.enabled ? "#81979d" : control.primary ? "#263b40" : "#e7f0ef"
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    background: Rectangle {
        radius: 6
        gradient: Gradient {
            GradientStop { position: 0; color: !control.enabled ? "#243c45" : control.primary ? (control.down ? "#c49d55" : "#f5d99d") : control.hovered ? "#365762" : "#294853" }
            GradientStop { position: 1; color: !control.enabled ? "#243c45" : control.primary ? (control.down ? "#b68d49" : "#d8af64") : "#203b46" }
        }
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#a4e8ef" : control.danger ? "#be6e6d" :
                      control.primary ? "#efd192" : "#43606a"
    }
}

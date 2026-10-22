import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 40
    padding: 12
    rightPadding: 34
    indicator: Canvas {
        x: control.width - width - 14
        y: (control.height - height) / 2
        width: 10
        height: 6
        onPaint: {
            let c = getContext("2d");
            c.clearRect(0, 0, width, height);
            c.beginPath(); c.moveTo(1,1); c.lineTo(5,5); c.lineTo(9,1);
            c.strokeStyle = "#cee2e5"; c.lineWidth = 1.5; c.stroke();
        }
    }
    background: Rectangle {
        color: control.hovered ? "#30515e" : "#264752"
        radius: 5
        border.color: control.activeFocus ? "#a4e8ef" : "#63838e"
        border.width: control.activeFocus ? 2 : 1
    }
}

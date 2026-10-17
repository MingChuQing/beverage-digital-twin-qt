import QtQuick 2.14
import QtQuick.Controls 2.14

Button {
    id: control
    property bool primary: false
    property bool danger: false
    implicitHeight: 34
    implicitWidth: Math.max(70, label.implicitWidth + 26)
    hoverEnabled: true
    font.pixelSize: 12
    contentItem: Text {
        id: label
        text: control.text
        font: control.font
        color: !control.enabled ? "#65768b" : control.primary ? "#082e2d" : control.danger ? "#ff939d" : "#d8e5f4"
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }
    background: Rectangle {
        radius: 5
        color: control.primary ? (control.down ? "#47baa5" : "#6ee3c3") : control.down ? "#243c52" : control.hovered ? "#1c334a" : "#15283c"
        border.color: control.primary ? "#6ee3c3" : control.danger ? "#73404c" : "#2a425a"
        opacity: control.enabled ? 1 : 0.45
        border.width: control.activeFocus ? 2 : 1
    }
}

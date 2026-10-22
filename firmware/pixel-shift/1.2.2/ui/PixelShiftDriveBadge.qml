// 自写控制页标识：数值来自控制器；只闪图标，数值及原厂括号不闪。
import QtQuick
import "qrc:/qt/qml/app/qml/components" as Components
Item {
    id: root
    property QtObject controller: null
    property real iconOpacity: 1
    property bool outlined: false
    readonly property string displayValue: controller ? controller.outputResolutionText : "—"
    implicitWidth: contentRow.implicitWidth
    width: implicitWidth
    height: 64
    Row {
    id: contentRow
    anchors.verticalCenter: parent.verticalCenter
    spacing: 12
    Item {
        objectName: "PixelShiftBadge_icon"
        opacity: root.iconOpacity
        width: 68; height: 54
        anchors.verticalCenter: parent.verticalCenter
        Rectangle { x: 18; y: 2; width: 46; height: 34; color: "transparent"; border.color: "white"; border.width: 2 }
        Rectangle { x: 10; y: 10; width: 46; height: 34; color: "transparent"; border.color: "white"; border.width: 2 }
        Rectangle { x: 2; y: 18; width: 46; height: 34; color: "transparent"; border.color: "white"; border.width: 2 }
        Text { x: 9; y: 20; text: "+"; color: "white"; font.pixelSize: 24 }
    }
    Row {
        objectName: "PixelShiftBadge_value"
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7 * 1.6
        Components.Bracket { anchors.verticalCenter: parent.verticalCenter; leftBracket: true; small: true; outlined: root.outlined }
        Text {
            objectName: "PixelShiftBadge_number"
            anchors.verticalCenter: parent.verticalCenter
            text: root.displayValue; color: "white"; font.pixelSize: 36; font.weight: Font.Medium
            style: root.outlined ? Text.Outline : Text.Normal; styleColor: "black"
        }
        Components.Bracket { anchors.verticalCenter: parent.verticalCenter; leftBracket: false; small: true; outlined: root.outlined }
    }
    }
}

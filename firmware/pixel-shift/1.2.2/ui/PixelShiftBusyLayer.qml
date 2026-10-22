// 自写体验版忙碌层；不冒充原厂状态机。实体快门由独立桥处理。
import QtQuick
Rectangle {
    id: root
    property QtObject controller: null
    // 模式确认期间由控制器防重复点击，不抢走菜单焦点。实际任务才覆盖取景。
    visible: controller !== null && controller.busy
    color: "#ed111111"
    z: 10000
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: function(event) { event.accepted = true }
    Keys.onReleased: function(event) { event.accepted = true }
    MouseArea { anchors.fill: parent; onPressed: function(mouse) { mouse.accepted = true } }
    Text {
        anchors.centerIn: parent; width: parent.width * 0.8
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
        color: "white"; font.pixelSize: 30
        text: root.controller && root.controller.backend && root.controller.backend.progressText
              ? root.controller.backend.progressText : root.controller ? root.controller.statusText : ""
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 32
        width: 220; height: 64; radius: 4; color: "#444444"
        visible: root.controller !== null && root.controller.cancelAvailable
        Text { anchors.centerIn: parent; color: "white"; font.pixelSize: 28; text: "退出本次拍摄" }
        MouseArea { anchors.fill: parent; onClicked: root.controller.requestCancel() }
    }
}

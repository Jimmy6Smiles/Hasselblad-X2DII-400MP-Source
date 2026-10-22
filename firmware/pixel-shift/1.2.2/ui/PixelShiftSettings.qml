// 私有 X2D II 参数页，使用当前固件原厂标题、返回按钮、参数行与选择器。
import QtQuick
import com.hasselblad.constants
import com.hasselblad.keys
import com.hasselblad.types
import "qrc:/qt/qml/app/qml/components"
import "qrc:/qt/qml/app/qml/mainmenu"
import "qrc:/qt/qml/app/qml/scripts/Keys.js" as MKeys
FocusScope {
    id: root
    objectName: "PixelShiftSettings"
    property QtObject controller: null
    property int currentRow: 1
    property bool keyboardHighlight: false
    property bool externalMenu: true
    readonly property bool editable: controller !== null && controller.optionsEditable
    readonly property bool popupActive: selector.active
    readonly property bool preventSwipe: selector.active
    readonly property int keyOverride: HblmTypes.E_CameraKeyOption_OverrideHalfPress
    signal closeRequested()
    focus: true
    function closePopup() { selector.active = false; keyboardHighlight = false; root.forceActiveFocus() }
    function valueText(row) {
        if (row === 0) return controller ? controller.captureProfile.displayName : "—"
        if (!controller) return "—"
        return row === 1 ? controller.initialDelay + " 秒" : controller.keepMode ? "保持" : "退出"
    }
    function openRow(row) {
        if (!editable || row < 1 || row > 2) return false
        currentRow = row; selector.active = true; return true
    }
    Rectangle { anchors.fill: parent; color: Constants.menuBackgroundColor }
    MouseArea { anchors.fill: parent }
    MenuHeader {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        externalMenu: true
        showBackArrow: true
        showSeparator: false
        text: [{ context: "", text: "像素超频", color: Constants.settingsMenuHeaderSuffixFontColor }]
        onClose: root.closeRequested()
    }
    Column {
        anchors { top: header.bottom; left: parent.left; right: parent.right }
        Repeater {
            model: ["像素数量", "初始延迟", "拍摄结束后"]
            delegate: Item {
                id: row
                required property int index
                required property string modelData
                objectName: "PixelShiftSettings_row" + index
                width: root.width
                height: Constants.menuListItemDefaultHeight
                Rectangle {
                    anchors.fill: parent; color: Constants.highlightColor
                    visible: root.keyboardHighlight && !selector.active && root.currentRow === row.index
                }
                Item {
                    anchors {
                        left: parent.left; right: parent.right; top: parent.top
                        topMargin: 16 * 1.6
                        leftMargin: Constants.settingsMenuSettingLeftMargin
                        rightMargin: Constants.settingsMenuSettingRightMargin
                    }
                    height: Constants.settingsMenuSettingFontSize
                    // 与 DropdownDelegate / BaseDelegate 相同的标签及数值样式。
                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        text: row.modelData
                        color: Constants.settingsMenuSettingFontColor
                        font.pixelSize: Constants.settingsMenuSettingFontSize
                        font.weight: Constants.menuItemLabelFontWeight
                        verticalAlignment: Text.AlignVCenter
                        opacity: root.editable && row.index > 0 ? 1.0 : Constants.menuItemDisabledTextOpacity
                    }
                    Text {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: root.valueText(row.index)
                        color: Constants.settingsMenuSettingFontColor
                        font.family: Constants.latinFont
                        font.pixelSize: Constants.settingsMenuTextFontSize
                        font.weight: Constants.menuItemValueFontWeight
                        verticalAlignment: Text.AlignVCenter
                        opacity: root.editable && row.index > 0 ? 1.0 : Constants.menuItemDisabledTextOpacity
                    }
                }
                MouseArea { anchors.fill: parent; enabled: root.editable && row.index > 0; onClicked: root.openRow(row.index) }
            }
        }
    }
    Keys.onPressed: (event)=> {
        if (MKeys.pressedFnListAcc([KeyFn.Abort, KeyFn.JstkEscape], event)) root.closeRequested()
        else if (MKeys.pressedFnAcc(KeyFn.HalfPress, event)) root.closeRequested()
        else if (MKeys.pressedFnListAcc([KeyFn.ListUp, KeyFn.JstkUp], event)) { currentRow = 1; keyboardHighlight = true }
        else if (MKeys.pressedFnListAcc([KeyFn.ListDown, KeyFn.JstkDown], event)) { currentRow = 2; keyboardHighlight = true }
        else if (MKeys.pressedFnListAcc([KeyFn.Select, KeyFn.PopAccept], event)) openRow(currentRow)
    }
    Loader {
        id: selector
        anchors.fill: parent
        active: false
        source: "PixelShiftSelector.qml"
        onLoaded: {
            var values = [], labels = []
            if (root.currentRow === 1) {
                for (var n = 2; n <= 60; n++) { values.push(n); labels.push(n + " 秒") }
            } else { values = [0, 1]; labels = ["退出", "保持"] }
            item.optionKey = root.currentRow === 1 ? "initialDelay" : "keepMode"
            item.externalMenu = true; item.interactive = true; item.longList = root.currentRow === 1
            item.values = values; item.labels = labels
            item.propValue = root.currentRow === 1 ? root.controller.initialDelay : root.controller.keepMode ? 1 : 0
            item.setup(); item.forceActiveFocus()
        }
    }
    Connections {
        target: selector.item
        function onOptionChosen(key, value) { if (root.editable) root.controller.setOption(key, value) }
        function onClose() { root.closePopup() }
        function onCloseExternal() { root.closePopup(); root.closeRequested() }
    }
    Component.onCompleted: console.info("PS_SETTINGS_NATIVE_STYLE_LOADED")
}

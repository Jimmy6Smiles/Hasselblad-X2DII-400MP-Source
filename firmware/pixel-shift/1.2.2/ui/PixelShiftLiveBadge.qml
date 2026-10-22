// 实时取景与控制页共用状态；本组件无模式写入或拍摄调用。
import QtQuick
import "." as Shared
PixelShiftDriveBadge {
    controller: Shared.PixelShiftState.controller
    readonly property bool modeActive: controller.selected || controller.busy
    outlined: true
}

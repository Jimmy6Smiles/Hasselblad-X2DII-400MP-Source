// 同一 GUI 引擎唯一状态源；控制页和实时取景读取同一个控制器。
pragma Singleton
import QtQml
QtObject {
    id: root
    readonly property QtObject transport: PixelShiftTransport {}
    readonly property QtObject controller: PixelShiftModeController { backend: root.transport }
}

import QtQml
import "." as Shared
QtObject {
    readonly property QtObject controller: Shared.PixelShiftState.controller
    readonly property QtObject transport: Shared.PixelShiftState.transport
}

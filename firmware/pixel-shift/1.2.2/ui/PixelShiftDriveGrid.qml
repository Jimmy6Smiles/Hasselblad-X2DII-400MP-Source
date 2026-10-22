/*
 * vim: tabstop=4 softtabstop=4 shiftwidth=4 expandtab fileencoding=utf-8
 * encoding: utf-8
 * -*- coding: utf-8 -*-
 *
 * Copyright 2020 Victor Hasselblad AB.
 *
 * All information contained in or disclosed by this document is confidential
 * and proprietary of Victor Hasselblad AB. By accepting this material the
 * recipient agrees that this material and the information contained therein
 * will be held in confidence and will not be reproduced in whole or in part
 * without written permission from Victor Hasselblad AB.
 */

pragma ComponentBehavior: Bound

import QtQuick
import "qrc:/qt/qml/app/qml/components"
import com.hasselblad.constants
import com.hasselblad.types
import com.hasselblad.keys

import "qrc:/qt/qml/app/qml/components/buttons"
import "qrc:/qt/qml/app/qml/scripts/Keys.js" as MKeys

HblGridView {
    id: root
    objectName: "DriveModeGrid_root"
    property list<DriveModeListItem> srcModel
    property int driveMode: 0
    property var selectionHandler: null
    property real maxWidth: 0
    readonly property real sideMargin: 28 * 1.6
    readonly property real highlightWidth: 76 * 1.6
    readonly property real highlightHeight: 56 * 1.6
    // Make item wide enough to avoid spacing
    readonly property real itemWidth: (maxWidth - 2 * sideMargin - numCols * highlightWidth) / (numCols - 1) + highlightWidth
    readonly property real itemHeight: 116 * 1.6 * Math.min(1, 3 / Math.max(1, numRows)) // top of topItem to top of next item

    signal setDriveMode(int mode)
    signal close()
    signal openDialog(string submenuName)
    signal openPixelSettings()
    signal startVibration(int mode)

    model: srcModel
    interactive: false
    boundsBehavior: Flickable.StopAtBounds
    width: numCols * itemWidth
    height: numRows * itemHeight
    cellWidth: itemWidth
    cellHeight: itemHeight
    numRows: 3
    numCols: 2

    onCountChanged: updateCurrentIndex()
    onDriveModeChanged: updateCurrentIndex()
    onMovedWithKey: root.startVibration(HblmTypes.E_VibrationMode_SharpTick1Percent100)

    function updateCurrentIndex()
    {
        for (var i = 0; i < root.count; i++) {
            if (model[i].mode === root.driveMode) {
                root.currentIndex = i
                break
            }
        }
    }

    function setSelected(closePopup: bool)
    {
        if (!isIndexSelectable(root.currentIndex))
            return
        var selectedMode = root.model[root.currentIndex].mode
        if (root.selectionHandler) {
            if (root.selectionHandler(selectedMode) !== true)
                return
        } else {
            root.setDriveMode(selectedMode)
        }

        if (closePopup) {
            root.close()
        }
    }

    function isIndexSelectable(ix : int) : bool // Overrides HblGridView
    {
        if (!root.srcModel || ix < 0 || ix >= root.srcModel.length) {
            return false
        }
        var item = srcModel[ix]
        return item.writable
    }

    delegate: FocusScope {
        id: delegate
        // Model properties
        required property int index
        required property string iconText
        required property string icon
        required property string iconDisabled
        required property string submenuName
        required property bool writable

        focus: GridView.isCurrentItem
        width: root.cellWidth
        height: root.cellHeight
        enabled: writable

        PixelShiftFramedImage {
            text: qsTranslate("DriveModes", delegate.iconText)
            textColor: delegate.writable ? Constants.popupTextColor :
                                           Constants.popupTextDisabledColor
            textPixelSize: 18 * 1.6
            textSideMargin: 0 * 1.6
            textTopMargin: 6 * 1.6
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
            }
            width: parent.width
            highlightHeight: root.highlightHeight
            highlightWidth: root.highlightWidth
            focus: parent.GridView.isCurrentItem
            highlightWhenFocus: false
            forceFrameHighlight: root.currentIndex === delegate.index
            forceRectHighlight: root.srcModel[delegate.index].mode === root.driveMode
            extensionIcon: root.srcModel[delegate.index].mode === -400
            source: extensionIcon ? "" : delegate.writable ? delegate.icon : delegate.iconDisabled

            Keys.onPressed: (event)=> {
                if (!delegate.writable) {
                    return
                }

                /* Joystick center and AF-D should behave slightly different:
                 *  - Joystick center: Choose the drive mode and exit the popup
                 *  - AF-D: Enter the settings popup
                 */
                if (MKeys.pressedFnListAcc([KeyFn.Afd, KeyFn.Square], event)) {
                    if (root.srcModel[delegate.index].mode === -400) {
                        root.openPixelSettings()
                    } else if (delegate.submenuName !== "") {
                        root.openDialog(delegate.submenuName)
                    } else {
                        root.setSelected(true)
                    }
                } else if (MKeys.pressedFnAcc(KeyFn.PopAccept, event)) {
                    root.currentIndex = delegate.index
                    root.setSelected(true)
                } else if (MKeys.pressedFnAcc(KeyFn.JstkUp, event)) {
                    root.moveUp()
                } else if (MKeys.pressedFnAcc(KeyFn.JstkDown, event)) {
                    root.moveDown()
                } else if (MKeys.pressedFnListAcc([KeyFn.Up, KeyFn.Left], event)) {
                    root.moveLeft()
                } else if (MKeys.pressedFnListAcc([KeyFn.Down, KeyFn.Right], event)) {
                    root.moveRight()
                }
            }
            onPressed: {
                root.forceActiveFocus()
                var closePopup = delegate.index === root.currentIndex
                root.currentIndex = delegate.index
                root.setSelected(closePopup)
            }
        }
    }

    Component.onCompleted: updateCurrentIndex()
}

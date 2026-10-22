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
import "qrc:/qt/qml/app/qml/popups"

import com.hasselblad.constants
import com.hasselblad.menumodel
import com.hasselblad.types
import com.hasselblad.keys
import com.hasselblad.valueformatter

import "qrc:/qt/qml/app/qml/components"
import "qrc:/qt/qml/app/qml/mainmenu"
import "qrc:/qt/qml/app/qml/scripts/Keys.js" as MKeys

Popover {
    id: root
    objectName: "PopoverDriveMode_root"
    Component.onCompleted: console.info("PS_DRIVE_MENU_LOADED")

    property MainMenuViewModel mainMenuViewModel: null
    property list<DriveModeListItem> driveModeModel
    property int driveMode: 0
    property QtObject pixelShiftController: null
    function openPixelSettings() { pageSwipe.pixelPage = true; pageSwipe.showLoader(true) }

    readonly property list<DriveModeListItem> displayDriveModeModel: {
        var items = []
        for (var i = 0; i < driveModeModel.length; i++)
            items.push(driveModeModel[i])
        items.push(pixelShiftItem)
        return items
    }
    DriveModeListItem {
        id: pixelShiftItem
        mode: -400 // 仅限本地 UI，不是原厂协议枚举
        iconText: "像素超频"
        icon: "file:///dev/x2d2-menu-session-v1/pixel-shift.svg"
        iconDisabled: icon
        description: "六张位移合成约四亿像素。点击右侧设置参数。"
        footerText: root.pixelShiftController ? root.pixelShiftController.statusText : "机内后端尚未就绪，暂不可用"
        writable: root.pixelShiftController !== null && !root.pixelShiftController.busy
    }
    Connections {
        target: root.pixelShiftController
        function onNativeModeRequested(mode) {
            root.setDriveMode(mode)
            root.setExitDriveMode(mode)
        }
        function onStatusTextChanged() { root.updateFooter() }
    }
    readonly property int keyOverride: HblmTypes.E_CameraKeyOption_OverrideHalfPress // Used by KeyFocusHandler.cpp
    readonly property real leftSideWidth: 256 * 1.6
    readonly property real headerAreaHeight: 56 * 1.6
    readonly property real headerFontSize: 20 * 1.6
    readonly property int headerFontWeight: Font.Medium
    readonly property real footerExpansion: 44

    signal setDriveMode(int mode)
    signal setExitDriveMode(int mode)
    signal startVibration(int mode)

    headingSize: 0
    contentWidth: 564 * 1.6
    contentHeight: 426 * 1.6 + footerExpansion
    popupTopMargin: 15 * 1.6
    readonly property bool browsingPixelShift: grid.currentIndex >= 0 && grid.currentIndex < displayDriveModeModel.length && displayDriveModeModel[grid.currentIndex].mode === -400
    readonly property string displayedFooter: browsingPixelShift
        ? (pixelShiftController ? pixelShiftController.statusText : "机内后端尚未就绪，暂不可用") : d.footerText
    footerAreaHeight: displayedFooter !== "" ? 60 : 0
    footer: browsingPixelShift ? displayedFooter : qsTranslate("InfoTexts", displayedFooter)

    onActiveFocusChanged: {
        if (activeFocus) {
            grid.focus = true
        }
    }

    // User has clicked outside popup.
    function handleUserClickedOutsidePopup() : bool // Overrides PopupBase
    {
        var internalPopupActive = pageSwipe.popupActive
        if (internalPopupActive) {
            pageSwipe.item.closePopup()
        }

        // Return true to save and close popup
        return !internalPopupActive
    }

    function setSelected() // Overrides PopupBase
    {
        root.close()
    }

    function updateFooter()
    {
        var ixValid = grid.currentIndex >= 0 && grid.currentIndex < grid.count
        d.footerText = ixValid ? root.displayDriveModeModel[grid.currentIndex].footerText :  ""
    }

    QtObject {
        id: d
        property string footerText: ""
    }

    content: Item {
        anchors.fill: parent
        clip: true

        Row {
            anchors.fill: parent

            Item {
                id: left_side

                width: root.leftSideWidth
                height: parent.height

                Item {
                    id: left_header_area
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    height: root.headerAreaHeight

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Drive Mode")
                        color: Constants.colorWhite
                        font.pixelSize: root.headerFontSize
                        font.weight: root.headerFontWeight
                    }
                }

                PixelShiftDriveGrid {
                    id: grid
                    focus: true
                    srcModel: root.displayDriveModeModel
                    numRows: Math.ceil(srcModel.length / numCols)
                    driveMode: root.pixelShiftController && root.pixelShiftController.selected ? -400 : root.driveMode
                    maxWidth: parent.width
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 62 * 1.6
                    }

                    onOpenPixelSettings: root.openPixelSettings()
                    onCurrentIndexChanged: root.updateFooter()

                    onOpenDialog: (settingsList)=> {
                        pageSwipe.pixelPage = false
                        pageSwipe.settingsList = settingsList
                        pageSwipe.showLoader(true)
                    }
                    selectionHandler: function(mode) {
                        if (root.pixelShiftController)
                            return root.pixelShiftController.selectMode(mode)
                        if (mode === -400)
                            return false
                        root.setDriveMode(mode)
                        root.setExitDriveMode(mode)
                        return true
                    }

                    Component.onCompleted: {
                        startVibration.connect(root.startVibration)
                        close.connect(root.close)
                    }
                }
            }
            Separator {
                anchors {
                    top: parent.top
                    bottom: parent.bottom
                }
                color: Constants.popupBorderColor
                width: Constants.popupBorderWidth
            }
            Item {
                id: right_side
                width: parent.width - left_side.width
                height: parent.height
                readonly property bool isPixelShift: grid.currentIndex >= 0 && root.displayDriveModeModel[grid.currentIndex].mode === -400
                readonly property bool canShowSettings: isPixelShift || list.settingsList !== ""

                MouseArea {
                    id: right_side_ma
                    anchors {
                        fill: parent
                        topMargin: right_header_area.height
                    }
                    enabled: right_side.canShowSettings
                    onClicked: {
                        if (right_side.isPixelShift) { root.openPixelSettings(); return }
                        pageSwipe.pixelPage = false
                        pageSwipe.settingsList = list.settingsList
                        pageSwipe.showLoader(true)
                    }
                }

                Item {
                    id: right_header_area
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    height: root.headerAreaHeight

                    Text {
                        anchors.centerIn: parent
                        text: qsTranslate("DriveModes", list.headerText)
                        color: Constants.colorWhite
                        font.pixelSize: root.headerFontSize
                        font.weight: root.headerFontWeight
                    }
                }

                HblListView {
                    id: list
                    interactive: false
                    spacing: 32 * 1.6
                    readonly property string headerText: grid.currentIndex >= 0 ? root.displayDriveModeModel[grid.currentIndex].iconText : ""
                    readonly property string settingsList: grid.currentIndex >= 0 ? root.displayDriveModeModel[grid.currentIndex].submenuName : ""
                    readonly property string description: grid.currentIndex >= 0 ? root.displayDriveModeModel[grid.currentIndex].description : ""
                    readonly property real sideMargins: 14 * 1.6
                    readonly property real listTopMargin: 67 * 1.6
                    readonly property int labelFontWeight: Font.Medium
                    readonly property int valueFontWeight: Font.Normal

                    onSettingsListChanged: {
                        if (settingsList === "") {
                            SubMenu.closeMenu(0)
                            list.model = null
                        } else {
                            SubMenu.showMenu(0, settingsList)
                            list.model = SubMenu.getModel(0)
                        }
                    }
                    anchors {
                        fill: parent
                        topMargin: list.listTopMargin
                        leftMargin: list.sideMargins
                        rightMargin: list.sideMargins
                    }

                    Text {
                        visible: !right_side.isPixelShift && list.settingsList === ""
                        width: parent.width
                        fontSizeMode: Text.HorizontalFit
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 25 * 1.6
                        wrapMode: Text.Wrap
                        text: qsTranslate("DriveModes", list.description)
                        color: Constants.colorWhite
                    }

                    Column {
                        visible: right_side.isPixelShift
                        width: parent.width
                        spacing: list.spacing
                        Repeater {
                            model: ["像素数量", "初始延迟", "拍摄结束后"]
                            delegate: TextValueRow {
                                required property int index
                                required property string modelData
                                width: list.width
                                text: modelData
                                valueText: !root.pixelShiftController ? "—" : index === 0
                                    ? root.pixelShiftController.captureProfile.displayName : index === 1
                                    ? root.pixelShiftController.initialDelay + " 秒"
                                    : root.pixelShiftController.keepMode ? "保持" : "退出"
                                pixelSize: 25 * 1.6
                                labelFontWeight: list.labelFontWeight
                                valueFontWeight: list.valueFontWeight
                            }
                        }
                    }
                    delegate: TextValueRow {
                        id: delegate
                        // Model properties
                        required property int index
                        required property string text1
                        required property string itemName
                        required property bool itemEnabled
                        required property var propValue

                        width: list.width
                        text: delegate.text1
                        enabled: delegate.itemEnabled
                        valueText: ValueFormatter.getDisplayAltValue(delegate.itemName, delegate.propValue)
                        pixelSize: 25 * 1.6
                        labelFontWeight: list.labelFontWeight
                        valueFontWeight: list.valueFontWeight
                    }
                }
            }
        }

        PageSwipe {
            id: pageSwipe
            objectName: "PopoverDriveMode_pageSwipe"
            property string settingsList: ""
            property bool pixelPage: false
            readonly property bool popupActive: item && item.popupActive
            readonly property bool preventSwipe: item && item.preventSwipe
            readonly property int keyOverride: HblmTypes.E_CameraKeyOption_Listen // Used by KeyFocusHandler.cpp
            anchors.fill: parent
            parentItem: parent
            swipeEnabled: active &&
                          !root.mainMenuViewModel.screenEVF &&
                          !preventSwipe
            source: pixelPage ? "file:///dev/x2d2-menu-session-v1/PixelShiftSettings.qml" : Constants.appPrefix + "mainmenu/SettingsGeneric.qml"
            arguments: { "externalMenu": true }

            Keys.onPressed: (event)=> {
                if (MKeys.pressedFnListAcc([KeyFn.Abort, KeyFn.JstkEscape], event)) {
                    showLoader(false)
                } else if (MKeys.pressedFnAcc(KeyFn.HalfPress, event)) {
                    // Only save here. Popup will be closed when LV is started
                    grid.setSelected(false)
                } else if (MKeys.pressedFnAcc(KeyFn.Right, event)) {
                    // block key
                }
            }

            onLoaded: {
                if (pixelPage) { item.controller = root.pixelShiftController; item.forceActiveFocus(); return }
                var menuItem = (item as SettingsGeneric)
                menuItem.viewModel = Qt.binding( function() { return root.mainMenuViewModel })
                menuItem.populateModel(0, settingsList)
                menuItem.menuLabel = Qt.binding( function() { return qsTranslate("DriveModes", list.headerText) })
            }

            Connections {
                target: pageSwipe.item
                ignoreUnknownSignals: true
                function onCloseRequested() { pageSwipe.showLoader(false) }
                function onCloseExternal()
                {
                    pageSwipe.showLoader(false)
                    // Only save here. Popup will be closed when LV is started
                    grid.setSelected(false)
                }
            }

            // Border for loaded menu
            Rectangle {
                id: menuBorder

                anchors.fill: parent
                color: "transparent"
                border.width: Constants.popupBorderWidth
                border.color: Constants.popupBorderColor
            }
        }

        states: [
            State {
                name: "show_dialog"
                when: pageSwipe.show
            },
            State {
                name: ""
                PropertyChanges { grid.focus: true; restoreEntryValues: false }
            }
        ]
    }

    Component.onDestruction: {
        SubMenu.closeMenu(0)
    }
}

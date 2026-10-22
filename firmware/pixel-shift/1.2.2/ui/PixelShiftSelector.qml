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
import com.hasselblad.constants
import com.hasselblad.keys
import com.hasselblad.types

import "qrc:/qt/qml/app/qml/scripts/Keys.js" as MKeys
import "qrc:/qt/qml/app/qml/components"

PopupBackground {
    objectName: "ListSelectorSettings_root"
    id: root

    property string optionKey: ""
    property var values: []
    property var labels: []
    signal optionChosen(string key, int value)
    ListModel { id: optionModel }
    property string propName: ""
    property var propValue: null
    property bool externalMenu: false
    property bool interactive: false
    property bool longList: false
    readonly property int keyOverride: HblmTypes.E_CameraKeyOption_Listen // Used by KeyFocusHandler.cpp
    signal closeExternal()
    signal valueSaved();
    signal startVibration(int mode)

    popupMarginY: (root.height - Constants.menuDropDownHeight) / 2
    popupAnchor {
        leftMargin: 320
        topMargin: (root.longList ? 30 : 131.2)
        rightMargin: 40
        bottomMargin: (root.longList ? 30 : 131.2)
    }

    QtObject {
        id: d
        property int startIndex: 0
        property bool initialized: false
        readonly property real numberOfItemsVisibleInList: root.longList ? Constants.numberOfItemsVisibleInLongList :
                                                                           Constants.numberOfItemsVisibleInList
    }

    function setup() // Overrides PopupBase
    {
        // Set correct model before it is used by listview
        optionModel.clear()
        for (var n = 0; n < root.values.length; n++) optionModel.append({ text: root.labels[n], value: root.values[n] })
        calcStartIndex()
        d.initialized = true
    }

    function initCurrentIndex()
    {
        // Set focus on currently selected value
        // We do not want it to slowly "roll" to position, so we set duration
        // temporarily to 0
        var oldHighlightMoveDuration = list.highlightMoveDuration;
        list.highlightMoveDuration = 0;
        list.currentIndex = d.startIndex
        list.highlightMoveDuration = oldHighlightMoveDuration;
        list.visible = true
    }

    function calcStartIndex()
    {
        d.startIndex = Math.max(0, root.values.indexOf(Number(root.propValue)))
    }

    // Called when the user clicked in the list, not necessarily on the selected item,
    // then the clicked item will be set
    function itemSelected(ix: int)
    {
        list.highlightMoveDuration = 0
        list.currentIndex = ix
        root.setSelected()
        root.close()
    }

    // Called from parent to set selected value when the user clicked outside of the list,
    // then the selected (highlighted) item will be set
    function setSelected() // Overrides PopupBase
    {
        if (list.currentIndex >= 0 && list.currentIndex < root.values.length)
            root.optionChosen(root.optionKey, Number(root.values[list.currentIndex]))
    }

    Keys.onPressed: (event)=> {
        if (MKeys.pressedFnAcc(KeyFn.HalfPress, event)) {
            // Halfpress should save and close external popup (i.e. drive mode)
            setSelected()
            if (root.externalMenu) {
                root.closeExternal()
            } else {
                root.close()
            }
        } else if (MKeys.pressedFnListAcc([KeyFn.PopAccept, KeyFn.JstkRight], event)) {
            setSelected()
            root.close()
        } else if (MKeys.pressedFnListAcc([KeyFn.Abort, KeyFn.JstkLeft], event)) {
            root.close()
        } else if (MKeys.pressedFnAcc(KeyFn.ListUp, event)) {
            list.moveUp()
        } else if (MKeys.pressedFnAcc(KeyFn.ListDown, event)) {
            list.moveDown()
        } else if (MKeys.pressedFnAcc(KeyFn.NavKey, event)) {
            event.accepted = true //Prevent other navigation keys from propagating
        }
    }

    content: FocusScope {
        anchors {
            fill: parent
            margins: Constants.defaultBorderWidth
        }
        focus: true

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            color: Constants.highlightColor
            height: list.height / d.numberOfItemsVisibleInList
        }

        HblListView {
            id: list
            objectName: "ListSelectorSettings_list"
            clip: true
            focus: true
            anchors.fill: parent
            visible: false

            orientation: ListView.Vertical
            interactive: root.interactive
            keyNavigationEnabled: false
            highlightRangeMode: ListView.StrictlyEnforceRange
            preferredHighlightBegin: list.height / 2 - list.height / d.numberOfItemsVisibleInList / 2
            preferredHighlightEnd: list.height / 2 + list.height / d.numberOfItemsVisibleInList / 2
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveVelocity: 1280
            model: d.initialized ? optionModel : null

            onCurrentIndexChanged: {
                if (visible) {
                    // Swipe with finger gives vibration feedback
                    if (moving) {
                        root.startVibration(HblmTypes.E_VibrationMode_SharpTick1Percent100)
                    }
                }
            }
            onMovedWithKey: root.startVibration(HblmTypes.E_VibrationMode_SharpTick1Percent100)
            onModelChanged: {
                if (model) {
                    root.initCurrentIndex()
                }
            }

            delegate: ScaledDelegate {
                id: delegate
                // Model properties
                required property int index
                required property string text
                required property int value

                listView: list
                numberOfItemsVisibleInList: d.numberOfItemsVisibleInList

                Text {
                    id: textItem
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 0.05 * height
                    text: delegate.text
                    font.family: Constants.latinFont
                    color: parent.ListView.isCurrentItem ? Constants.highlightItemColor: Constants.itemColor
                    font.pixelSize: parent.height * 0.6 * delegate.scaleFactor
                    width: parent.width - 2 * Constants.settingsMenuSettingDropDownTextMargin
                    height: textMetrics.tightBoundingRect.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    fontSizeMode: Text.HorizontalFit
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    onClicked: root.itemSelected(delegate.index)
                }

                TextMetrics {
                    id: textMetrics
                    font: textItem.font
                    text: delegate.text
                }
            }

            ListGradient {
                anchors.fill: list
            }
        }

        // Add MouseArea under the scrollbar to block touch input and prevent user from closing
        // popup by accident when trying no interact with (the non-interactive) scrollbar.
        MouseArea {
            anchors {
                top: list.top
                left: list.right
                bottom: list.bottom
            }
            visible: root.longList
            width: Constants.popupListMarginX
            onClicked: {}

            Scrollbar {
                anchors {
                    top: parent.top
                    left: parent.left
                    bottom: parent.bottom
                    leftMargin: 10
                }
                heightRatio: list.visibleArea.heightRatio
                yRatio: list.visibleArea.yPosition
                edgeMarginY: 0
            }
        }
    }
}

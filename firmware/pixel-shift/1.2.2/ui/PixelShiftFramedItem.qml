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

import QtQuick
import com.hasselblad.constants

Item {
    id: root
    objectName: "FramedItem_root"

    property alias content: childContent.children

    // Focus settings
    property bool highlightWhenFocus: true
    property bool highlightRectWhenFocus: highlightWhenFocus
    property bool highlightFrameWhenFocus: highlightWhenFocus

    // Press settings
    property bool highlightWhenPressed: false
    property bool highlightRectWhenPressed: highlightWhenPressed
    property bool highlightFrameWhenPressed: highlightWhenPressed
    property bool showBoldTextWhenPressed: true

    // Forced highlight
    property bool forceHighlight: false
    property bool forceRectHighlight: forceHighlight
    property bool forceFrameHighlight: forceHighlight
    property bool forceFrameOutline: false

    //Text settings
    property bool forceBoldText: false
    property color textColor: Constants.colorWhite
    property real textPixelSize: 32
    property alias textWrapping: subheading.wrapMode
    property alias textOpacity: subheading.opacity
    property string text: ""
    property real textTopMargin: 0
    property real textSideMargin: 8

    // Highlight size
    property real highlightHeight: 0
    property real highlightWidth: 0

    // Misc
    property bool selectable: true // Item is interactive
    property bool acceptKeys: true // Key presses will be caught/blocked

    // Colors
    property color backColor: "transparent"
    property color rectPressColor: Constants.highlightColor
    property color rectFocusColor: Constants.highlightColor
    property color rectForcedHighlightColor: Constants.highlightColor
    property color framePressColor: Constants.highlightColor
    property color frameFocusColor: Constants.highlightColor
    property color frameForcedHighlightColor: Constants.highlightColor
    property color frameOutlineColor: Constants.framedItemOutlineColor

    // MouseArea
    property alias isPressed: mouseArea.pressed
    property alias touchEnabled: mouseArea.enabled

    signal clicked()
    signal pressAndHold()
    signal pressed()
    signal released()

    opacity: selectable ? 1 : 0.5
    width: 80
    height: {
        var h = childContent.childrenRect.height

        if (subheading.visible) {
            h += subheading.height + root.textTopMargin
        }

        return h
    }

    QtObject {
        id: d

        // Press/focus
        readonly property bool isPressed: mouseArea.containsMouse && mouseArea.pressed
        readonly property bool hasFocus: root.activeFocus

        // Rect highlight
        readonly property bool highlightRect: root.forceRectHighlight ||
                                              highlightRectFromPress ||
                                              highlightRectFromFocus
        readonly property bool highlightRectFromPress: root.highlightRectWhenPressed && isPressed && root.selectable
        readonly property bool highlightRectFromFocus: root.highlightRectWhenFocus && hasFocus

        // Rect color
        readonly property color rectColor: highlightRect ? rectHighlightColor : root.backColor
        readonly property color rectHighlightColor: root.forceRectHighlight ? root.rectForcedHighlightColor :
                                                    highlightRectFromPress ? root.rectPressColor :
                                                    highlightRectFromFocus ? root.rectFocusColor :
                                                                             root.backColor
        // Frame highlight
        readonly property bool highlightFrame: root.forceFrameHighlight ||
                                              highlightFrameFromPress ||
                                              highlightFrameFromFocus
        readonly property bool highlightFrameFromPress: root.highlightFrameWhenPressed && isPressed
        readonly property bool highlightFrameFromFocus: root.highlightFrameWhenFocus && hasFocus

        // Frame color
        readonly property color frameColor: highlightFrame ? frameHighlightColor :
                                            root.forceFrameOutline ? root.frameOutlineColor : root.backColor
        readonly property color frameHighlightColor: root.forceFrameHighlight ? root.frameForcedHighlightColor :
                                                     highlightFrameFromPress ? root.framePressColor :
                                                     highlightFrameFromFocus ? root.frameFocusColor :
                                                                               root.backColor

        // Bold text
        property bool showBoldText: root.forceBoldText || (root.showBoldTextWhenPressed && isPressed)
    }

    Rectangle {
        id: bg_highlight
        anchors.centerIn: childContent

        width: root.highlightWidth
        height: root.highlightHeight
        color: d.rectColor
        border.color: d.frameColor
        border.width: Constants.framedItemBorderwidth
        radius: 0
    }

    Keys.onReturnPressed: (event)=> {
        if (acceptKeys) {
            root.clicked()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    Item {
        id: childContent
        height: children.length > 0 ? children[0].height : 0
        width: children.length > 0 ? children[0].width : 0
        anchors {
            top: parent.top
            horizontalCenter: parent.horizontalCenter
        }
    }

    TextMetrics {
        id: subHeadingMetric
        font.family: subheading.font.family
        font.pixelSize: subheading.font.pixelSize
        text: subheading.text.length > 0 ? subheading.text : " "
        font.weight:  d.showBoldText ? Font.Bold : Font.Normal
        font.letterSpacing: subheading.font.letterSpacing
    }

    Item {
        id: subHeadingContent
        anchors {
            top: childContent.bottom
            horizontalCenter: parent.horizontalCenter
            topMargin: root.textTopMargin
        }
        width: root.width - 2 * root.textSideMargin
        height: subHeadingMetric.height

        Text {
            id: subheading
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.height
            text: root.text
            color: root.textColor
            font.family: Constants.latinFont
            font.weight: d.showBoldText ? Font.Bold : Font.Normal
            font.pixelSize: root.textPixelSize
            // Letter spacing is changed to avoid line break when using the wider bold font
            font.letterSpacing: d.showBoldText ? -0.72 : 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }

    MouseArea {
        id: mouseArea
        property bool gotPress: false
        anchors.fill: parent
        onClicked: {
            /* Workaround for two separate bugs:
             * 1. An extra click is emitted while still being pressed causing two clicks to be sent
             * 2. Swiping over a button can incorrectly be detected as a click.
             */
            if (gotPress && !pressed) {
                root.clicked()
            }
        }
        onCanceled: gotPress = false
        onExited: gotPress = false
        onPressAndHold: root.pressAndHold()
        onPressed: {
            gotPress = true
            root.pressed()
        }
        onReleased: {
            if (!containsMouse) {
                root.released()
            }
        }
    }
}

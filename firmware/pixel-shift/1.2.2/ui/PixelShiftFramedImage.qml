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

import "qrc:/qt/qml/app/qml/components"

PixelShiftFramedItem {
    id: root
    property bool extensionIcon: false
    property alias source: imageItem.source
    property alias image: imageItem
    width: highlightWidth * 2

    content: Item {
        width: parent.width
        height: root.highlightHeight

        Item {
            visible: root.extensionIcon
            width: 100; height: 64; anchors.centerIn: parent
            Rectangle { x: 30; y: 3; width: 56; height: 38; radius: 3; color: "transparent"; border.color: "#dddddd"; border.width: 2 }
            Rectangle { x: 22; y: 11; width: 56; height: 38; radius: 3; color: "transparent"; border.color: "#dddddd"; border.width: 2 }
            Rectangle { x: 14; y: 19; width: 56; height: 38; radius: 3; color: "transparent"; border.color: "#dddddd"; border.width: 2 }
            Rectangle { x: 32; y: 37; width: 20; height: 2; color: "#dddddd" }
            Rectangle { x: 41; y: 28; width: 2; height: 20; color: "#dddddd" }
        }
        ScaledImage {
            id: imageItem
            anchors.centerIn: parent
        }
    }
}

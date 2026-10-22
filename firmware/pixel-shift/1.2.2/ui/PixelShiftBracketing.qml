/*
 * vim: tabstop=4 softtabstop=4 shiftwidth=4 expandtab fileencoding=utf-8
 * encoding: utf-8
 * -*- coding: utf-8 -*-
 *
 * Copyright 2021 Victor Hasselblad AB.
 *
 * All information contained in or disclosed by this document is confidential
 * and proprietary of Victor Hasselblad AB. By accepting this material the
 * recipient agrees that this material and the information contained therein
 * will be held in confidence and will not be reproduced in whole or in part
 * without written permission from Victor Hasselblad AB.
 */

import QtQuick
import "qrc:/qt/qml/app/qml/exposescreen"
import "file:///dev/x2d2-menu-session-v1" as PixelShift

import com.hasselblad.types
import com.hasselblad.constants
import com.hasselblad.valueformatter

ExposeScreen {
    objectName: "BracketingScreen_root"
    id: root
    readonly property var pixelController: PixelShift.PixelShiftState.controller
    readonly property bool pixelBusy: pixelController.busy
    property int pixelFinished: 0
    function rememberFrames() {
        if (!pixelBusy) { pixelFinished = 0; return }
        if (PixelShift.PixelShiftState.transport.initialDelayActive) { pixelFinished = 0; return }
        var count = Number(viewModel.numFinishedBracketingFrames)
        if (PixelShift.PixelShiftState.transport.phase === "capture" && isFinite(count) && count >= 0 && count <= 6)
            pixelFinished = Math.max(pixelFinished, count)
        if (PixelShift.PixelShiftState.transport.captureVerified) pixelFinished = 6
    }
    onPixelBusyChanged: rememberFrames()
    Component.onCompleted: rememberFrames()
    Connections {
        target: root.viewModel
        function onNumFinishedBracketingFramesChanged() { root.rememberFrames() }
    }
    Connections {
        target: PixelShift.PixelShiftState.transport
        function onCaptureVerifiedChanged() { root.rememberFrames() }
        function onInitialDelayActiveChanged() { root.rememberFrames() }
        function onJobTokenChanged() { root.pixelFinished = 0 }
    }

    readonly property string exposureBracketingAdjustText: viewModel.expBracketingAdjustAsString + "EV"
    readonly property string multiShotAdjustText: viewModel.multiShotAdjustAsString
    readonly property string focusBracketingStepSizeAsString: ValueFormatter.getDisplayValue("focus_bracketing_step_size", viewModel.focusBracketingStepSize)
    readonly property bool isLongFocusBracketingText: !viewModel.exposureBracketingActive && !viewModel.multiShotActive &&
                                                      (viewModel.focusBracketingStepSize === HblmTypes.E_FocusBracketingStepSizes_ExtraSmall ||
                                                       viewModel.focusBracketingStepSize === HblmTypes.E_FocusBracketingStepSizes_ExtraLarge)


    QtObject {
        id: d
        property bool nativeInitDelay: root.viewModel.bracketingDelayTime > 0
        readonly property bool isInitDelay: root.pixelBusy ? PixelShift.PixelShiftState.transport.initialDelayActive : nativeInitDelay
    }

    headerText: pixelBusy ? "像素超频" : viewModel.exposureBracketingActive ? qsTr("Exposure Bracketing") :
                viewModel.multiShotActive ? qsTr("Multishot") :
                                            qsTr("Focus Bracketing")
    showButtons: pixelBusy ? pixelController.cancelAvailable : !viewModel.multiShotActive
    showExposuresLeft: root.showHeader && viewModel.saveToLocalStorage
    btn2.text: qsTr("Exit")
    onAbortPressed: {
        if (pixelBusy) { pixelController.requestCancel(); return }
        if (root.showButtons && !viewModel.multiShotActive) {
            viewModel.stopBracketing()
        }
    }

    ExposeIconText {
        id: mainText
        anchors.fill: parent
        visible: d.isInitDelay
        text1.text: root.pixelBusy ? PixelShift.PixelShiftState.transport.delayRemaining + "" : root.viewModel.tdur(root.viewModel.intervalTimerCountdown)
        text1.font.pixelSize: 100 * 1.6
        iconSpacing: 16 * 1.6
        icon.source: "image://svg/ic_drive_mode_self_timer_big"
    }

    ExposeProgress {
        id: progress
        readonly property real rowFontSize: root.isLongFocusBracketingText ? 80 :
                                                                             60 * 1.6
        anchors.fill: parent
        visible: !d.isInitDelay
        icon1.source: root.pixelBusy ? "file:///dev/x2d2-menu-session-v1/pixel-shift.svg" : root.viewModel.bracketingIcon
        icon1Spacing: 7 * 1.6
        rowSpacing: 40 * 1.6

        text1.text: root.pixelBusy ? "" : root.viewModel.exposureBracketingActive ? root.exposureBracketingAdjustText :
                                                              root.viewModel.multiShotActive ? "":
                                                              root.focusBracketingStepSizeAsString
        text1.font.pixelSize: rowFontSize
        text2.text: root.pixelBusy ? root.pixelFinished + "/6" : root.viewModel.numFinishedBracketingFrames + "/" + root.viewModel.numBracketingFrames
        text2.font.pixelSize: rowFontSize
        text3.visible: text3.text !== "0"
        text3.text: root.pixelBusy ? PixelShift.PixelShiftState.transport.progressText : root.viewModel.tdur(root.viewModel.exposureTimerCount)
        text3.width: root.pixelBusy ? Math.max(0, progress.width - 96 * 1.6) : text3.implicitWidth
        text3.font.pixelSize: root.pixelBusy ? 28 * 1.6 : 50 * 1.6
        text3.wrapMode: root.pixelBusy ? Text.Wrap : Text.NoWrap
        text3.maximumLineCount: root.pixelBusy ? 2 : 2147483647
        text3.elide: root.pixelBusy ? Text.ElideRight : Text.ElideNone
        text3.lineHeight: root.pixelBusy ? 1.2 : 1.0
        progressValue: {
            if (root.pixelBusy) return root.pixelFinished / 6
            var num = root.viewModel.numBracketingFrames
            var val = num < 0 ? -1 :
                      num === 0 ? 0 :
                      (1.0 * root.viewModel.numFinishedBracketingFrames) / num;
            return val
        }
    }

    Connections {
        target: root.viewModel
        function onIntervalTimerCountdownChanged()
        {
            if (root.viewModel.intervalTimerCountdown === 0) {
                d.nativeInitDelay = false
            }
        }
    }
}

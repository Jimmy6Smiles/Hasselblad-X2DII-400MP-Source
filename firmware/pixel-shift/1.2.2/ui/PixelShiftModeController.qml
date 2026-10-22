// 自写的离线接入契约；没有设备操作或拍摄命令。
import QtQml

QtObject {
    id: root
    property QtObject backend: null
    readonly property int extensionMode: -400
    // 当前唯一实现的采集配置；新增档位须同时实现后端，不能只改显示开启。
    readonly property var captureProfile: ({ id: "auto6_400", nominalMegapixels: 400, displayName: "四亿" })
    readonly property string outputResolutionText: captureProfile.nominalMegapixels + "M"
    // 保留在控制页而非弹窗内；关闭弹窗不能丢失恢复义务。
    property bool restorePending: false
    property bool armed: false
    property bool pendingSelection: false
    property int pendingNativeMode: -1
    readonly property bool asyncBackend: backend !== null && backend.asynchronousControl === true
    function syncRouting(): void {
        if (!asyncBackend || backend.connected !== true) return
        if (pendingSelection && backend.commandPending === true) return
        if (backend.confirmedArmed === true) {
            armed = true
            pendingSelection = false
        } else if (!backend.busy && !backend.recovery && !backend.modeTransitionPending) {
            armed = false
            if (!backend.commandPending) restorePending = false
            if (pendingNativeMode >= 0) {
                var mode = pendingNativeMode
                pendingNativeMode = -1
                pendingSelection = false
                restorePending = false
                nativeModeRequested(mode)
            }
        }
    }
    property string lastError: ""
    property int initialDelay: 2
    property bool keepMode: false
    readonly property bool optionsEditable: !busy && !pendingSelection && !optionUpdatePending
        && (!asyncBackend ? !restorePending : backend.connected && !backend.recovery && !backend.commandPending)
    property int requestedDelay: 2
    property bool requestedKeep: false
    property bool optionUpdatePending: false
    function setOption(key: string, value: int): bool {
        if (!optionsEditable) return false
        if (asyncBackend && selected) {
            if (!((key === "initialDelay" && value >= 2 && value <= 60)
                  || (key === "keepMode" && (value === 0 || value === 1)))) return false
            requestedDelay = key === "initialDelay" ? value : initialDelay
            requestedKeep = key === "keepMode" ? value === 1 : keepMode
            optionUpdatePending = true
            if (!backend.updateOptions(requestedDelay, requestedKeep)) { optionUpdatePending = false; return false }
            return true
        }
        if (key === "initialDelay" && value >= 2 && value <= 60) {
            initialDelay = value
            return true
        }
        if (key === "keepMode" && (value === 0 || value === 1)) {
            keepMode = value === 1
            return true
        }
        return false
    }
    // 断联不能把仍在拍摄/写入的任务当作结束。恢复必须由同一任务明确确认。
    property bool taskUnsettled: false
    property string activeJob: ""
    property string activeGeneration: ""
    readonly property bool backendBusy: backend !== null && backend.busy === true
    readonly property bool busy: backendBusy || taskUnsettled
    readonly property bool recoveryRequired: taskUnsettled && (!backendBusy)
    function syncTaskState(): void {
        if (backend === null) return
        if (backend.busy === true) {
            taskUnsettled = true
            if (activeJob === "") {
                activeJob = backend.jobToken
                activeGeneration = backend.generationToken
            }
        } else if (taskUnsettled && backend.settled === true
                   && activeJob !== "" && activeGeneration !== ""
                   && activeJob === backend.jobToken
                   && activeGeneration === backend.generationToken) {
            taskUnsettled = false
            activeJob = ""
            activeGeneration = ""
            if (asyncBackend && !backend.confirmedArmed
                    && (backend.failedSafely === true || backend.cancelledSuccessfully === true || (!keepMode && backend.savedSuccessfully === true))) {
                armed = false
                restorePending = false
                completedExitRequested()
            }
        }
    }
    onBackendChanged: syncTaskState()
    onBackendBusyChanged: syncTaskState()
    property Connections taskConnection: Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onStateUpdated() { root.syncTaskState(); root.syncRouting() }
        function onSettledChanged() { root.syncTaskState() }
        function onJobTokenChanged() { root.syncTaskState() }
        function onGenerationTokenChanged() { root.syncTaskState() }
        function onConfirmedArmedChanged() { root.syncRouting() }
        function onModeTransitionPendingChanged() { root.syncRouting() }
        function onCommandPendingChanged() { root.syncRouting() }
        function onCommandFinished(ok) {
            if (root.optionUpdatePending) {
                root.optionUpdatePending = false
                if (ok && root.backend.confirmedDelay === root.requestedDelay
                        && root.backend.confirmedKeep === root.requestedKeep) {
                    root.initialDelay = root.requestedDelay
                    root.keepMode = root.requestedKeep
                } else root.lastError = "参数未获后台确认，请重新设置"
            }
            if (!ok) { root.pendingSelection = false; root.lastError = "操作未获确认，请核对后台状态" }
            // 接受 arm 请求不等于硬件接管确认；等待期间仍允许用户选择原厂模式退出。
            if (root.pendingSelection && root.pendingNativeMode < 0
                    && !root.backend.confirmedArmed && !root.backend.busy)
                root.pendingSelection = false
            root.syncRouting()
        }
    }
    property bool cancelRequested: false
    readonly property bool cancelAvailable: backendBusy && !cancelRequested
        && activeJob === backend.jobToken && activeGeneration === backend.generationToken
        && backend.cancelEnabled === true
    onBusyChanged: { if (!busy) cancelRequested = false }
    // 供拍摄页的“退出”使用；不调用原厂间隔模式，也不由 GUI 删除文件。
    function requestCancel(): bool {
        if (!cancelAvailable) return false
        var job = backend.jobToken
        var generation = backend.generationToken
        // 任务编号用字符串传递，避免 JS 大整数丢失精度。
        if (typeof job !== "string" || typeof generation !== "string"
                || !/^[1-9][0-9]*$/.test(job) || !/^[1-9][0-9]*$/.test(generation)) {
            lastError = "任务身份未确认，暂不能退出"
            return false
        }
        cancelRequested = true
        try {
            if (backend.cancel(job, generation) !== true) {
                lastError = "退出尚未获确认，请等待状态核对"
                return false
            }
        } catch (error) {
            lastError = "退出通信异常，请等待状态核对"
            return false
        }
        lastError = ""
        // 接收请求不代表已停拍/清理；后台确认之前继续保持 busy。
        return true
    }
    readonly property bool available: backend !== null && ((asyncBackend && backend.connected === true
        && (backend.experimentalReady === true || backend.canNormalizeFormat === true)) || (!asyncBackend
        && backend.firmwareMatched === true && backend.captureReady === true
        && backend.synthesisReady === true && backend.storageReady === true
        // 不要求 M 档；后端必须能锁定首帧实际曝光并在所有退出路径恢复。
        // 不能用“已切为 M”冒充此能力，详见 EXPOSURE_CONTRACT.md。
        && backend.exposureLatchReady === true
        // 六帧仅作为内部临时输入；校验及保存成功后只发布一张合成图。
        && backend.singleOutputReady === true
        && backend.physicalShutterReady === true && backend.ready === true))
    readonly property bool selected: armed
    readonly property string statusText: lastError !== "" ? lastError
        : pendingSelection ? "正在确认拍摄模式，请稍候"
        : recoveryRequired ? "任务结束尚未确认，请保留电源和存储卡"
        : busy ? (cancelRequested ? (asyncBackend ? "正在停止本次拍摄；原片暂保留" : "正在停止并清理本次拍摄，请等待") : "正在采集或合成，请等待")
        : asyncBackend && backend.lastError !== "" ? backend.lastError
        : asyncBackend && restorePending && !selected ? "正在确认快门接管；可选择单张退出"
        : !available ? "机内后端尚未就绪，暂不可用"
        : selected ? "" : "请固定相机并拍摄静止主体"
    signal nativeModeRequested(int mode)
    signal completedExitRequested()

    function selectMode(mode: int): bool {
        lastError = ""
        if (busy || pendingSelection) {
            lastError = "任务进行中，不能切换拍摄模式"
            return false
        }
        if (mode === extensionMode) {
            if (!available) {
                lastError = "拍摄接入尚未完成，不能启用像素超频"
                return false
            }
            if (selected)
                return true
            if (restorePending) {
                lastError = "请先切回原厂模式并确认状态恢复"
                return false
            }
            // 即使超时、异常或返回失败，也不能假定设备完全未改变。
            restorePending = true
            try {
                // arm 必须是已确认且可回滚的状态切换，不能触发曝光。
                if (backend.arm(initialDelay, keepMode) !== true) {
                    lastError = "启用未获确认，模式保持不变"
                    return false
                }
            } catch (error) {
                lastError = "启用接口失败，模式未确认"
                return false
            }
            if (asyncBackend) { pendingSelection = true; syncRouting(); return true }
            armed = true
            return true
        }
        if ([0, 6, 2, 3, 4, 5].indexOf(mode) < 0) {
            lastError = "拒绝未知拍摄模式"
            return false
        }
        if (restorePending) {
            try {
                if (backend === null || backend.disarm() !== true) {
                    lastError = "原厂状态恢复尚未确认，暂不切换"
                    return false
                }
            } catch (error) {
                lastError = "恢复接口失败，暂不切换"
                return false
            }
            if (asyncBackend) { pendingNativeMode = mode; pendingSelection = true; syncRouting(); return true }
            restorePending = false
            armed = false
        }
        nativeModeRequested(mode)
        return true
    }
}

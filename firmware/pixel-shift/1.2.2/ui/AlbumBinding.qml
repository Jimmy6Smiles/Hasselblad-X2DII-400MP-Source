Item {
id: psAlbumBinding
property string currentItem: d.itemModel ? d.itemModel.itemName : ""
property string endpoint: root.pixelShiftEndpoint
property string boundItem: ""
property string regionEndpoint: ""
property int generation: 0
property var pending: null
property int failures: 0
readonly property bool ready: boundItem !== "" && boundItem === currentItem && regionEndpoint !== ""
function refresh() {
if (pending !== null || (ready && psZoomDetail.status === Image.Loading)) return
if (!/^\/(ssd|cfe)\/[1-9][0-9]{2}HASBL\/B[0-9]{7}$/.test(currentItem)
|| !/^http:\/\/127\.0\.0\.1:[0-9]{4,5}$/.test(endpoint)) return
var serial = generation
var item = currentItem
var base = endpoint
var xhr = new XMLHttpRequest()
pending = xhr
deadline.restart()
xhr.onreadystatechange = function() {
if (xhr.readyState !== XMLHttpRequest.DONE || serial !== generation || pending !== xhr) return
pending = null
deadline.stop()
var result = null
try { if (xhr.status === 200) result = JSON.parse(xhr.responseText) } catch (error) {}
if (result && result.item === item && result.width === 23310 && result.height === 17482
&& /^[0-9a-f]{16}$/.test(result.token)) {
failures = 0
regionEndpoint = base + "/region" + item + "/" + result.token
boundItem = item
} else if (xhr.status === 0 || xhr.status >= 500) {
failed()
} else {
boundItem = ""
regionEndpoint = ""
}
}
xhr.open("GET", base + "/info" + item)
xhr.send()
}
function failed() {
failures++
if (failures >= 3) { boundItem = ""; regionEndpoint = "" }
}
function timedOut() {
var old = pending
pending = null
if (old !== null) old.abort()
failed()
}
function invalidate() {
failures = 0
generation++
boundItem = ""
regionEndpoint = ""
var old = pending
pending = null
if (old !== null) old.abort()
deadline.stop()
refresh()
}
onCurrentItemChanged: invalidate()
onEndpointChanged: invalidate()
Component.onCompleted: refresh()
Component.onDestruction: { generation++; if (pending !== null) pending.abort() }
Timer { id: pollBinding; interval: 2000; repeat: true; running: psAlbumBinding.currentItem !== ""; onTriggered: psAlbumBinding.refresh() }
Timer { id: deadline; interval: 15000; onTriggered: psAlbumBinding.timedOut() }
}

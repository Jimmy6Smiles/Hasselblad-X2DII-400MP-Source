Item {
id: psZoomLayer
property bool backendReady: root.pixelShiftSelected && root.viewModel.zoomed
property string boundItem: root.pixelShiftBoundItem
property string currentItem: d.itemModel ? d.itemModel.itemName : ""
property string endpoint: psAlbumBinding.regionEndpoint
property real rawX: Math.max(0, flick.contentX*flick.scaleFactor-image_surface.x)
property real rawY: Math.max(0, flick.contentY*flick.scaleFactor-image_surface.y)
property real pixelScale: flick.scaleFactor > 0 ? 1/flick.scaleFactor : 0
property int rawWidth: 23310
property int rawHeight: 17482
readonly property bool interacting: flick.moving || pinchArea.pinch.active || xAnimation.running || yAnimation.running
readonly property bool eligible: backendReady && boundItem !== "" && currentItem === boundItem
&& endpoint.indexOf("http://127.0.0.1:") === 0 && pixelScale > 0
&& width > 0 && height > 0 && width <= 1024 && height <= 1024
&& rawX >= 0 && rawY >= 0 && rawX < rawWidth && rawY < rawHeight
readonly property int regionX: Math.floor(rawX)
readonly property int regionY: Math.floor(rawY)
readonly property int regionWidth: Math.min(Math.ceil(width / pixelScale) + 1, rawWidth-regionX)
readonly property int regionHeight: Math.min(Math.ceil(height / pixelScale) + 1, rawHeight-regionY)
readonly property string requestedUrl: eligible ? endpoint + "/" + regionX + "/" + regionY + "/" + regionWidth + "/" + regionHeight : ""
property string settledUrl: ""
property int retries: 0
readonly property bool detailReady: eligible && !interacting && psZoomDetail.status === Image.Ready && psZoomDetail.source.toString() === requestedUrl
function schedule() {
psZoomSettle.stop()
psZoomRetry.stop()
psZoomDeadline.stop()
retries = 0
if (requestedUrl !== "" && !interacting) psZoomSettle.restart()
}
function loadLatest() {
if (!eligible || interacting) return
if (settledUrl === requestedUrl && (psZoomDetail.status === Image.Ready || psZoomDetail.status === Image.Loading)) return
settledUrl = ""
settledUrl = requestedUrl

}
function retry() {
psZoomDeadline.stop()
if (eligible && !interacting && retries < 2) {
retries++
psZoomRetry.restart()
}
}
clip: true
onRequestedUrlChanged: schedule()
onInteractingChanged: schedule()
Timer { id: psZoomSettle; interval: 180; onTriggered: psZoomLayer.loadLatest() }
Timer { id: psZoomRetry; interval: 400; onTriggered: psZoomLayer.loadLatest() }
Timer { id: psZoomDeadline; interval: 5000 }
Image {
id: psZoomDetail
source: psZoomLayer.settledUrl
asynchronous: true
cache: false
visible: false
x: (psZoomLayer.regionX-psZoomLayer.rawX)*psZoomLayer.pixelScale
y: (psZoomLayer.regionY-psZoomLayer.rawY)*psZoomLayer.pixelScale
width: psZoomLayer.regionWidth*psZoomLayer.pixelScale
height: psZoomLayer.regionHeight*psZoomLayer.pixelScale
smooth: true
onStatusChanged: {
if (status === Image.Ready) psZoomDeadline.stop()
else if (status === Image.Error) psZoomLayer.retry()
}
}
HblImageShader {
id: psZoomFactoryDisplay
fragmentShader: "qrc:/shaders/photo.frag.qsb"
image: psZoomDetail
gainmapImage: null
lutImage: d.disableLut ? null : fullWindowSdrLut
overExposureRunning: root.viewModel.overExposureRunning
x: psZoomDetail.x
y: psZoomDetail.y
width: psZoomDetail.width
height: psZoomDetail.height
visible: psZoomLayer.detailReady && (!lutImage || lutImage.hasRetainedContent)
}
x: flick.scaleFactor > 0 ? Math.max(0, image_surface.x/flick.scaleFactor-flick.contentX) : 0
y: flick.scaleFactor > 0 ? Math.max(0, image_surface.y/flick.scaleFactor-flick.contentY) : 0
width: Math.max(0, Math.min(flick.width-x, (rawWidth-rawX)*pixelScale))
height: Math.max(0, Math.min(flick.height-y, (rawHeight-rawY)*pixelScale))
z: 1
}

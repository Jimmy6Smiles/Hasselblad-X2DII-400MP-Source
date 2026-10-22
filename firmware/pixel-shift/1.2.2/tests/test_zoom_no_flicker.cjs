// Exercise actual QML JavaScript functions with timer/image/network doubles.
const fs = require('fs'), vm = require('vm'), assert = require('assert');
const qml = fs.readFileSync(__dirname + '/../ui/AlbumBinding.qml', 'utf8') + fs.readFileSync(__dirname + '/../ui/DetailLayer.qml', 'utf8');
assert(qml.includes('fragmentShader: "qrc:/shaders/photo.frag.qsb"'));
assert(qml.includes('lutImage: d.disableLut ? null : fullWindowSdrLut'));
assert(!qml.includes('photo_oes_target.frag.qsb'));
assert(qml.includes('gainmapImage: null'));
function body(name) {
  const start = qml.indexOf('function ' + name + '(');
  assert(start >= 0);
  let end = qml.indexOf('{', start), depth = 1, i = end + 1;
  for (; depth; i++) { if(qml[i] === '{') depth++; if(qml[i] === '}') depth--; }
  return qml.slice(start, i);
}
const timer = () => ({count:0, stop(){}, restart(){this.count++;}});
const c = {pending:null, failures:0, generation:0, boundItem:'/cfe/999HASBL/B0000001',
 currentItem:'/cfe/999HASBL/B0000001', regionEndpoint:'region', endpoint:'http://127.0.0.1:38407',
 ready:true, eligible:true, interacting:false, requestedUrl:'region/1/2/320/240',
 settledUrl:'region/1/2/320/240', retries:0,
 Image:{Ready:1,Loading:2,Error:3}, psZoomDetail:{status:1},
 deadline:timer(), psZoomSettle:timer(), psZoomRetry:timer(), psZoomDeadline:timer()};
const requests=[];
c.XMLHttpRequest = function(){requests.push(this);this.open=()=>{};this.send=()=>{};this.abort=()=>{this.aborted=true;};};
c.XMLHttpRequest.DONE=4;
vm.createContext(c);
for(const n of ['failed','timedOut','invalidate','refresh','schedule','loadLatest','retry']) vm.runInContext(body(n),c);
let writes=0, value=c.settledUrl;
Object.defineProperty(c,'settledUrl',{get:()=>value,set:v=>{writes++;value=v;}});
c.loadLatest(); assert.equal(writes,0); // ready same region stays visible
c.psZoomDetail.status=2;c.loadLatest();assert.equal(writes,0); // no duplicate in flight
c.refresh();assert.equal(requests.length,0); // polling cannot queue behind decode
c.psZoomDetail.status=1;c.refresh();assert.equal(requests.length,1);
c.timedOut();assert.equal(c.boundItem,c.currentItem);assert.equal(c.regionEndpoint,'region');
assert(requests[0].aborted); // stale callback ignored
requests[0].readyState=4;requests[0].status=404;requests[0].onreadystatechange();
assert.equal(c.regionEndpoint,'region');
c.refresh();const ok=requests.at(-1);ok.readyState=4;ok.status=200;
ok.responseText=JSON.stringify({item:c.currentItem,width:23310,height:17482,token:'0123456789abcdef'});
ok.onreadystatechange();assert.equal(c.failures,0);
c.requestedUrl='region/10/20/320/240';c.loadLatest();assert.equal(writes,2);
assert.equal(c.psZoomDeadline.count,0); // slow decode is not repeatedly cancelled
c.psZoomDetail.status=3;c.retry();c.retry();c.retry();assert.equal(c.psZoomRetry.count,2);
c.failed();c.failed();assert(c.boundItem);c.failed();assert.equal(c.boundItem,''); // bounded outage
c.boundItem=c.currentItem;c.regionEndpoint='old';c.currentItem='/cfe/999HASBL/B0000002';
c.invalidate();assert.equal(c.boundItem,'');assert.equal(c.regionEndpoint,'');assert.equal(c.failures,0);
const missing=requests.at(-1);missing.readyState=4;missing.status=404;missing.onreadystatechange();
assert.equal(c.boundItem,'');
console.log('PASS: same region, in-flight decode, poll suppression, transient timeout, stale reply, recovery, new region, bounded retry, outage, item change, missing file');

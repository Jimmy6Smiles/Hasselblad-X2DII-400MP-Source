"""Run the actual QML in Qt with a mock native ContentModel singleton."""
from pathlib import Path
import os,json
os.environ['QT_QPA_PLATFORM']='offscreen'
from PySide6.QtCore import QObject,Property,Slot,QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine,QQmlComponent,qmlRegisterSingletonInstance,QJSValue
class Model(QObject):
    def __init__(self):
        super().__init__();self.calls=[];self.browsing=False;self.loading_=False;self.select=False;self.path_='/cfe/DCIM/999HASBL'
    isBrowsing=Property(bool,lambda s:s.browsing)
    loading=Property(bool,lambda s:s.loading_)
    fileSelectMode=Property(bool,lambda s:s.select)
    path=Property(str,lambda s:s.path_)
    @Slot(str)
    def onPathChanged(self,path): self.calls.append(path)
app=QGuiApplication([]);model=Model()
qmlRegisterSingletonInstance(Model,'com.hasselblad.storage',1,0,'ContentModel',model)
engine=QQmlEngine();p=Path(__file__).resolve().parents[1]/'ui'
c=QQmlComponent(engine,QUrl.fromLocalFile(str(p/'PixelShiftTransport.qml')));o=c.create()
assert o is not None,[x.toString() for x in c.errors()]
o.setProperty('connected',True);o.setProperty('settled',True)
checks=[]
def queue(job='1',**kw):
    state=dict(busy=False,recovery=False,settled=True,error='SAVED_DEFAULT_FOLDER_SIX_DELETED',jobToken=job,generationToken='99');state.update(kw)
    o.queueAlbumRefresh(engine.toScriptValue(state))
def drain():
    for _ in range(4):o.refreshCompletedAlbum()
queue();drain();assert len(model.calls)==1;checks.append('success refreshes same path')
queue();drain();assert len(model.calls)==1;checks.append('duplicate polling is idempotent')
queue('2',error='JOB_FAILED');drain();assert len(model.calls)==1;checks.append('failure cannot refresh')
model.browsing=True;queue('3');drain();assert len(model.calls)==1
model.browsing=False;drain();assert len(model.calls)==2;checks.append('defer active browse')
model.select=True;queue('4');drain();assert len(model.calls)==2
model.select=False;drain();assert len(model.calls)==3;checks.append('preserve multi-select')
queue('5');queue('6',busy=True);drain();assert len(model.calls)==3;checks.append('new capture cancels stale refresh')
o.setProperty('connected',False);queue('7');drain();assert len(model.calls)==3
o.setProperty('connected',True);drain();assert len(model.calls)==4;checks.append('disconnect defers refresh')
model.path_='';queue('8');drain();assert len(model.calls)==4;checks.append('empty path safe')
model.path_='/ssd/DCIM/106HASBL';queue('9');drain();assert model.calls[-1]==model.path_;checks.append('preserve other browsed device')
print(json.dumps({'passed':True,'checks':checks},indent=2))

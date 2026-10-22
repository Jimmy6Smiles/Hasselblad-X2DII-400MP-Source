#!/usr/bin/python3
"""仅本地主机 JSON 替身，不连接设备。"""
import json,os,signal,sys,time
from pathlib import Path
p=Path(os.environ['AUTO6_STATE']);s=json.loads(p.read_text());a=sys.argv[1:]
s['calls'].append(a);b=s.get('behavior','normal')
def save():p.write_text(json.dumps(s))
if a[:3]==['-s','camera','-m']:
 assert a==['-s','camera','-m','do_exposure_extended2','true','E_SessionOptions_SimpleDriveMode|E_SessionOptions_SkipAutoFocus|E_SessionOptions_MultiShotExposure','30000']
 if s.get('drive',0)!=0:save();print('MF switch in focus bracketing.');sys.exit(1)
 s['captures']+=1;s['frames']=5 if b=='five' else 6;save()
 if b=='foreign_drive':s['drive']=3;save();sys.exit(1)
 if b=='hang':time.sleep(10)
 if b=='crash':os.kill(os.getppid(),signal.SIGKILL)
 if b=='cancel':os.kill(os.getppid(),signal.SIGTERM)
 if b=='overflow':print('X'*5000)
 if b=='noack':sys.exit(1)
 sys.exit(0)
assert a[:3]==['-s','camera','-p'] and len(a) in (4,5)
prop=a[3]
names=['SingleShot','Auto4','Auto6','Auto16','Auto4B','Auto6B','Auto16B']
if len(a)==5:
 if prop=='drive_mode':
  values=['Single','Continuous','SelfTimer','Interval','ExposureBracketing','FocusBracketing','SlowBurst'];v=['E_DriveModes_'+n for n in values].index(a[4]);s['writes'].append([prop,v])
  if (b=='drive_reject' and v==0) or (b=='drive_restorefail' and v!=0):save();sys.exit(1)
  s['drive']=v;save()
  if b=='drive_lost' and v==0:sys.exit(1)
  if b=='drive_cancel' and v==0:os.kill(os.getppid(),signal.SIGTERM)
  if b=='drive_crash' and v==0:os.kill(os.getppid(),signal.SIGKILL)
  sys.exit(0)
 if prop=='image_format':
  values=['Raw','RawJpeg','Jpeg','RawHeif','Heif'];s['image_format']=['E_ImageFormat_'+v for v in values].index(a[4]);s['writes'].append([prop,s['image_format']]);save();sys.exit(0)
 if prop in ('ael_user','ael_reset_after_exposure'):
  assert a[4] in ('true','false');s[prop]=a[4]=='true';s['writes'].append([prop,s[prop]]);save();sys.exit(0)
 assert prop=='multishot_control_mode'
 value=['E_MultiShotMode_'+n for n in names].index(a[4]);s['writes'].append(value)
 if b=='reject' and value==2:save();sys.exit(1)
 if b=='restorefail' and value==0:save();sys.exit(1)
 s['multi']=value
 if b=='setlost' and value==2:save();sys.exit(1)
 if b=='setcancel' and value==2:save();os.kill(os.getppid(),signal.SIGTERM)
 save();sys.exit(0)
if prop=='multishot_control_mode':
 v=s['multi'];v='E_MultiShotMode_'+names[v]+'('+str(v)+')'
elif prop=='image_format':
 f=s.get('image_format',0);v='E_ImageFormat_'+['Raw','RawJpeg','Jpeg','RawHeif','Heif'][f]+'('+str(f)+')'
elif prop=='drive_mode':
 d=s.get('drive',0);v='E_DriveModes_'+['Single','Continuous','SelfTimer','Interval','ExposureBracketing','FocusBracketing','SlowBurst'][d]+'('+str(d)+')'
elif prop=='exposure_status':v='E_ExposureStatus_Started(2)' if b=='busy' or (b=='drainbusy' and s['captures']) else 'E_ExposureStatus_None(0)'
elif prop=='exposures_processing_counter':v=0
elif prop=='exposures_in_multishot_session':v=s['frames']
elif prop=='ael_reset_after_exposure':v='true' if s.get(prop,False) else 'false'
elif prop in ('ael_user','ae_lock_enabled'):v='true' if b!='nolock' and s.get('ael_user',True) else 'false'
elif prop=='exp_mode':v='E_ExpMode_AperturePriority(1)'
elif prop in ('av_selected','tv_selected','sv_selected'):v={'av_selected':32,'tv_selected':252,'sv_selected':60}[prop]
else:raise AssertionError(prop)
save();print(prop+' = '+str(v))

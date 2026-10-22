"""Host-only journal/boot reconciliation fixtures; no camera connection."""
import hashlib,json,os,struct,subprocess,tempfile
from host_compiler import compiler
from pathlib import Path
B=Path(__file__).resolve().parent
N=B.parent/'src/recovery'
def packed(fmt,*values):
 data=struct.pack(fmt,*values);h=2166136261
 for v in data:h=((h^v)*16777619)&0xffffffff
 return data+struct.pack('<I',h)
checks=[]
with tempfile.TemporaryDirectory(prefix='ps-reboot-') as temp:
 root=Path(temp);data=root/'data';ram=root/'ram';jobs=data/'jobs';jobs.mkdir(parents=True);ram.mkdir();boot=root/'boot'
 current=b'48eb07a4-a680-479b-bc15-c00a2e062c42';old=b'943ed129-987a-406f-801e-5d8d1b9964bf';boot.write_bytes(current+b'\n')
 mock=root/'mock_odin.py';mock.write_bytes((B/'mock_reboot_odin.py').read_bytes());mock.chmod(0o700)
 (root/'mock_auto6.py').write_bytes((B/'mock_auto6.py').read_bytes())
 exe=root/'reconcile'
 subprocess.run([*compiler(),'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-I'+str(N),'-DRP_DATA="'+str(data)+'"','-DRP_RAM="'+str(ram)+'"','-DRP_BOOT="'+str(boot)+'"','-DODIN_PATH="'+str(mock)+'"',str(N/'reboot_reconcile.c'),'-o',str(exe)],check=True)
 for index,case in enumerate(('restored','ae_not_restored','multi_active','format_changed','drive_changed','busy','same_boot','corrupt','missing','symlink','inspect','archive_exists','active_error','error_query_failed','boot_single')):
  token=str(100000+index);job=jobs/token;job.mkdir(mode=0o700)
  pending=data/'pending';pending.unlink(missing_ok=True);pending.write_text(token);pending.chmod(0o600)
  (ram/'pending.reviewed').unlink(missing_ok=True)
  bid=current if case=='same_boot' else old
  records={'ae.journal':packed('<8s40s7iI',b'X2DAEG1',bid,1,0,0,1,32,252,60,1),
   'capture.journal':packed('<8s40siiI',b'X2DCAP2',bid,0,6 if case=='boot_single' else 0,3),
   'format.journal':packed('<8s40sII',b'X2DFMT1',bid,0,2)}
  for name,blob in records.items():(job/name).write_bytes(blob)
  if case=='corrupt':(job/'ae.journal').write_bytes(b'bad')
  if case=='missing':(job/'ae.journal').unlink()
  if case=='symlink':(job/'ae.journal').unlink();(job/'ae.journal').symlink_to(job/'capture.journal')
  if case=='archive_exists':(job/'reboot-resolved.pending').write_text('DO_NOT_OVERWRITE')
  original={p.name:p.read_bytes() for p in job.iterdir()}
  state=root/'state.json';state.write_text(json.dumps(dict(active_error=case=='active_error',error_query_failed=case=='error_query_failed',behavior='busy' if case=='busy' else 'normal',multi=2 if case=='multi_active' else 0,drive=1 if case=='drive_changed' else 0,image_format=1 if case=='format_changed' else 0,frames=6,captures=0,calls=[],writes=[],ael_user=case=='ae_not_restored',ael_reset_after_exposure=True)))
  p=subprocess.run([exe]+(['--inspect'] if case=='inspect' else []),env=dict(os.environ,AUTO6_STATE=str(state)),capture_output=True,text=True,timeout=10)
  actual=json.loads(state.read_text());assert not actual['writes'] and not actual['captures']
  assert all((job/n).read_bytes()==blob for n,blob in original.items())
  if case in ('restored','boot_single','format_changed'):assert p.returncode==0 and not pending.exists() and (job/'reboot-resolved.pending').read_text()==token
  elif case=='inspect':assert p.returncode==0 and pending.read_text()==token and not (ram/'pending.reviewed').exists()
  elif case=='archive_exists':assert p.returncode==2 and pending.read_text()==token and not (ram/'pending.reviewed').exists()
  else:assert p.returncode==0 and pending.read_text()==token and (ram/'pending.reviewed').read_text().endswith(' 0\n'),(case,p)
  checks.append(case)
print(json.dumps(dict(passed=True,cases=checks),indent=2))

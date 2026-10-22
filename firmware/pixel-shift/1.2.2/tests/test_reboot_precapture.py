"""Offline reproduction of retained empty AE preflight rejection."""
import json, os, struct, subprocess, tempfile
from pathlib import Path
from host_compiler import compiler
B=Path(__file__).resolve().parent
N=B.parent/'src/recovery'
def record(boot,phase=1):
    raw=struct.pack('<8s40siiI',b'X2DCAP2',boot,0,0,phase);h=2166136261
    for b in raw:h=((h^b)*16777619)&0xffffffff
    return raw+struct.pack('<I',h)
cases=('rejected','inspect','busy','same_boot','corrupt','capture_present','format_present','ae_written','unknown_file','symlink','hardlink','missing_proof','nonterminal','active_error','multi_active','drive_changed','archive_resume','archive_collision')
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);data=root/'data';ram=root/'ram';data.mkdir();ram.mkdir();(data/'jobs').mkdir()
    boot=root/'boot';current=b'c25d3196-6e88-4a7c-8904-62b308a3c926';old=b'26e02897-4313-44ec-8978-ded19315d9fc';boot.write_bytes(current+b'\n')
    for n in ('mock_reboot_odin.py','mock_auto6.py'):
        (root/n).write_bytes((B/n).read_bytes());(root/n).chmod(0o700)
    exe=root/'reconcile'
    subprocess.run([*compiler(),'-std=c11','-O2','-Wno-format-truncation','-DRP_DATA="'+str(data)+'"','-DRP_RAM="'+str(ram)+'"','-DRP_BOOT="'+str(boot)+'"','-DODIN_PATH="'+str(root/'mock_reboot_odin.py')+'"',str(N/'reboot_reconcile.c'),'-o',str(exe)],check=True)
    for i,case in enumerate(cases):
        job=data/'jobs'/str(1000+i);job.mkdir(mode=0o700);pending=data/'pending';pending.write_text(job.name);pending.chmod(0o600)
        mode=data/'mode-prepare.journal';mode.unlink(missing_ok=True);mode.write_bytes(record(current if case=='same_boot' else old));mode.chmod(0o600)
        (job/'ae.journal').write_bytes(b'x' if case=='ae_written' else b'')
        (job/'before.bin').write_bytes(b'listing')
        (job/'pipeline.log').write_text('83152 PREPARE_NO_DELETE\n85497 PREFLIGHT_REJECTED_NO_SETTINGS_OR_CAPTURE\n'+('99999 OTHER\n' if case=='nonterminal' else ''))
        (job/'children.log').write_text('PS_CAPTURE_STAGE DELAY 2\nAE_SNAPSHOT_FAILED idle_before\nAE_PREFLIGHT_NO_WRITES\nSTREAM_SHADOW_END fault=0 cancelled=0 candidates=0 prepared=0 finished=0\n')
        if case=='missing_proof':(job/'children.log').write_text('')
        if case=='corrupt':mode.write_bytes(b'bad')
        if case in ('capture_present','format_present','unknown_file'):(job/({'capture_present':'capture.journal','format_present':'format.journal'}.get(case,'unexpected'))).write_bytes(b'')
        if case=='symlink':(job/'ae.journal').unlink();(job/'ae.journal').symlink_to(job/'before.bin')
        if case=='hardlink':(job/'ae.journal').unlink();os.link(job/'before.bin',job/'ae.journal')
        if case=='archive_resume':mode.rename(job/'precap-mode.archived')
        if case=='archive_collision':(job/'precap-mode.archived').write_bytes(mode.read_bytes())
        state=root/'state.json';state.write_text(json.dumps(dict(active_error=case=='active_error',behavior='busy' if case=='busy' else 'normal',multi=2 if case=='multi_active' else 0,drive=1 if case=='drive_changed' else 0,frames=0,captures=0,calls=[],writes=[])))
        original={p.name:p.read_bytes() for p in job.iterdir()}
        p=subprocess.run([exe,'--inspect' if case=='inspect' else '--resume'],env=dict(os.environ,AUTO6_STATE=str(state)),capture_output=True,text=True,timeout=15)
        observed=json.loads(state.read_text());assert not observed['writes'] and not observed['captures']
        assert all((job/n).read_bytes()==b for n,b in original.items())
        if case in ('rejected','archive_resume'):
            assert p.returncode==0 and not pending.exists() and not mode.exists(),(case,p)
            assert (job/'reboot-resolved.pending').read_text()==job.name
            assert (job/'precap-mode.archived').read_bytes()==record(old)
        elif case=='inspect':assert p.returncode==0 and pending.exists() and mode.exists()
        else:assert p.returncode!=0 and pending.exists(),(case,p)
print('PASS pre-capture reboot recovery:',len(cases),'scenarios; no setters or capture')

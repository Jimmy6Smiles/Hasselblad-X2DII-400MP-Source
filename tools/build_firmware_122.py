"""Offline build of the four recovery-related programs. Never access a camera."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'firmware/pixel-shift/1.2.2'

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--ndk',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--host-tag',default='darwin-x86_64' if sys.platform=='darwin' else 'windows-x86_64' if sys.platform=='win32' else 'linux-x86_64')
    p.add_argument('--candidate',action='store_true',help='Build explicitly unverified changed sources; retain baseline mismatch report')
    p.add_argument('--targets',nargs='+',choices=['integrated-job','integration-service','shutter.so','reboot-reconcile'])
    a=p.parse_args()
    cc=a.ndk/'toolchains/llvm/prebuilt'/a.host_tag/'bin'/('clang.exe' if a.host_tag.startswith('windows') else 'clang')
    a.out.mkdir(parents=True,exist_ok=True)
    flags=['--target=aarch64-linux-android28','-std=c11','-O2','-Wall','-Wextra','-Werror',
           '-Wno-unused-function','-Wno-misleading-indentation','-s']
    shutter=SOURCE/'src/shutter'
    defines=['PS_FAST_VIRTUAL=1','PS_FAST_REFRESH=1','PS_FAST_DIAGNOSTICS=1','PS_FAST_RECONFIRM=1',
             'PS_FAST_ARM_RECONFIRM=1','NATIVE_STATE_SCOPE','NATIVE_NINE_TILE_TEST','NATIVE_JOB_RUNTIME','NATIVE_FACTORY_GRID']
    # __FILE__ is passed to the native container lookup for diagnostics. Keep the
    # historical string to reproduce the existing binary after relocating sources.
    old=r'D:\X2DII-DevKit\Jimmy6Ma\Hasselblad-X2DII-Extensions\extensions\pixel-shift\firmware\1.3.16.2'
    jobs={
        'integrated-job':(['src/coordinator/integrated_job.c'],['-fPIE','-pie','-DPS_NATIVE_COLOR_JOB']),
        'integration-service':(['src/service/integration_service.c'],['-fPIE','-pie']),
        'shutter.so':(['src/shutter/'+n for n in ('shutter_bridge.c','notification_sret.S','native_pregdc_hook.c','afc_eshutter.c')],
                      ['-fPIC','-shared','-I'+str(shutter),'-ffile-prefix-map='+str(SOURCE)+'='+old,*['-D'+d for d in defines]]),
        'reboot-reconcile':(['src/recovery/reboot_reconcile.c'],['-fPIE','-pie']),
    }
    expected=json.loads((SOURCE/'binary-baseline.json').read_text()) if (SOURCE/'binary-baseline.json').exists() else {}
    report={}
    for name,(sources,extra) in jobs.items():
        if a.targets and name not in a.targets:continue
        cmd=[str(cc),*flags,*extra,*[str(SOURCE/s) for s in sources]]
        if name=='shutter.so':cmd+=['-ldl']
        subprocess.run([*cmd,'-o',str(a.out/name)],check=True)
        digest=hashlib.sha256((a.out/name).read_bytes()).hexdigest()
        report[name]={'sha256':digest,'expected':expected.get(name),'matches_package':digest==expected.get(name)}
    (a.out/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    if a.candidate:
        print('CANDIDATE ONLY: not camera validated; baseline hashes were not changed.')
    elif expected and not all(r['matches_package'] for r in report.values()):
        raise SystemExit('Binary mismatch; do not substitute into MOD without review.')

if __name__=='__main__':main()

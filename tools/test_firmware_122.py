"""POSIX/WSL host mocks only; no camera or user photographs are accessed."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]/'firmware/pixel-shift/1.2.2'
sys.path.insert(0, str(root/'tests'))
from host_compiler import compiler
# These standalone build directories intentionally carry identical helpers.
reference=(root/'src/coordinator/first_frame_metadata.h').read_bytes()
for header in (root/'src').glob('*/first_frame_metadata.h'):
    assert header.read_bytes()==reference, f'metadata helper drift: {header}'
for name in ('auto_prepare.c','overlap_prepare.c'):
    assert (root/'src/coordinator'/name).read_bytes()==(root/'src/merge'/name).read_bytes(), name
subprocess.run([sys.executable,str(root/'tests/test_reboot_recovery.py')],check=True)
subprocess.run([sys.executable,str(root/'tests/test_reboot_precapture.py')],check=True)
names=['test_capture_storage','test_mode_retry','test_failed_job_return',
       'test_direct_service_state','test_rebind','test_half_press','test_stage_directory','test_precapture_failure','test_boot_review','test_optional_aperture','test_subject_distance']
with tempfile.TemporaryDirectory(prefix='ps122-host-') as directory:
    for name in names:
        output=Path(directory)/name
        subprocess.run([*compiler(),'-O2','-I',str(root/'src/shutter'),str(root/'tests'/f'{name}.c'),'-o',str(output)],check=True)
        subprocess.run([str(output)],check=True)
print('PASS: reboot reconciliation (15 scenarios) and 11 host suites; no device accessed.')

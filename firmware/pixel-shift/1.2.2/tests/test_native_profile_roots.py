from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "shutter" / "native_pregdc_hook.c"


def test_immutable_profile_is_separate_from_runtime_state():
    text = SOURCE.read_text(encoding="utf-8")
    assert '#define NATIVE_RUNTIME_ROOT "/dev/x2d2-pregdc-trial"' in text
    assert "#define NATIVE_PROFILE_ROOT NATIVE_RUNTIME_ROOT" in text
    assert 'real_open(NATIVE_PROFILE_ROOT"/authorized.once"' in text
    for name in ("diagnostic-iq.bin", "candidate.conf", "candidate.sp"):
        assert f'NATIVE_PROFILE_ROOT"/{name}"' in text
    for name in ("hook.log", "job.ack"):
        assert f'ROOT"/{name}"' in text


def test_diagnostic_log_is_not_an_enablement_gate():
    text = SOURCE.read_text(encoding="utf-8")
    log_open = text.index('logfd=real_open(ROOT"/hook.log"')
    enabled = text.index("enabled=1", log_open)
    between = text[log_open:enabled]
    assert "if(logfd<0)return" not in between


def test_shared_runtime_uses_factory_gui_identity_and_mapped_components():
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index("#ifdef NATIVE_SHARED_RUNTIME")
    end = text.index("#else", start)
    branch = text[start:end]
    assert '"/system/bin/camera-gui"' in branch
    assert "NATIVE_SHARED_GUI_HOST" in branch
    assert "NATIVE_SHARED_GUI_PLUGIN" in branch
    assert '"/dev/x2d2-unified-boot/mode"' not in branch

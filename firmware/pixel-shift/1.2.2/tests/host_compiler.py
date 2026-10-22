"""Host fixtures only; production Android builds never include this shim."""
import os, shlex, sys
from pathlib import Path

def compiler():
    args = shlex.split(os.environ.get("CC", "cc"))
    if sys.platform == "darwin":
        args += ["-include", str(Path(__file__).with_name("host_compat.h")),
                 "-I" + str(Path(__file__).with_name("host_include"))]
    return args

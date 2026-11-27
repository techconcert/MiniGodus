"""Runs the real firmware/store.cpp on a simulated flash (test/store_sim): whole-game save and load in 6-slot records,
power cuts inside erases and writes, the r5 one-slot record and the crash quarantine."""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def main():
    tmp = tempfile.mkdtemp(prefix="store_sim_")
    try:
        for f in ("store.cpp", "store.h"):
            shutil.copy(os.path.join(ROOT, "firmware", f), tmp)    # a copy, so that "prefs.h" etc. resolve to the fakes
        exe = os.path.join(tmp, "sim_store")
        cmd = ["g++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-Wall", "-Wextra", "-Werror",
               "-I", os.path.join(ROOT, "test", "store_sim", "fake"), "-I", tmp, "-I", os.path.join(ROOT, "test"), "-I", os.path.join(ROOT, "lib", "gamecore", "src"),
               "-I", os.path.join(ROOT, "lib", "gqsafe", "src"), "-o", exe, os.path.join(ROOT, "test", "store_sim", "sim_store.cpp"),
               os.path.join(tmp, "store.cpp")]
        import glob
        cmd += sorted(glob.glob(os.path.join(ROOT, "lib", "gamecore", "src", "*.cpp"))) + sorted(glob.glob(os.path.join(ROOT, "lib", "gqsafe", "src", "*.cpp")))
        subprocess.check_call(cmd, cwd=ROOT)
        subprocess.check_call([exe], cwd=ROOT)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_store_sim: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

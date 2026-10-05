#!/usr/bin/env python3
"""Packages the Linux build as a self-contained folder and .tar.gz.

    scripts/package_linux.py <build dir> <conda prefix> <label> <out dir>

makes <out dir>/Cadjitsu-<label>-Linux-x86_64/ and its .tar.gz:

    cadjitsu                 launcher (run this)
    bin/Cadjitsu, bin/fonts, bin/qt.conf
    lib/                     every library the app and its Qt plugins load from the
                             conda environment (the system's C library, X11 and GL
                             drivers stay the system's), and the denoiser's devices
    plugins/                 the Qt plugins it needs
    etc/fonts/               fontconfig's configuration

The app's RPATH is rewritten to $ORIGIN/../lib. Fails if anything in the
package still resolves into the conda environment.
"""
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile

PLUGIN_DIRS = ["platforms", "xcbglintegrations", "iconengines", "imageformats", "platforminputcontexts",
               "platformthemes", "egldeviceintegrations", "generic", "tls"]


def ldd(path, env=None):
    out = subprocess.run(["ldd", path], capture_output=True, text=True, env=env).stdout
    libs = {}
    for line in out.splitlines():
        m = re.match(r"\s*(\S+) => (\S+)", line)
        if m:
            libs[m.group(1)] = m.group(2)
    return libs


def set_rpath(path, new):
    """Overwrites the ELF's DT_RPATH / DT_RUNPATH string in place (the new one must be shorter)."""
    out = subprocess.run(["readelf", "-d", path], capture_output=True, text=True).stdout
    m = re.search(r"\((?:RPATH|RUNPATH)\)\s+Library r(?:un)?path: \[(.*)\]", out)
    if not m:
        return
    old = m.group(1).encode()
    data = bytearray(open(path, "rb").read())
    at = data.find(old + b"\0")
    if at < 0 or len(new) > len(old):
        sys.exit(f"cannot rewrite the RPATH of {path}")
    data[at:at + len(old)] = new.encode() + b"\0" * (len(old) - len(new))
    open(path, "wb").write(data)


def main():
    build, prefix, label, out = sys.argv[1:5]
    prefix = os.path.realpath(prefix)
    name = f"Cadjitsu-{label}-Linux-x86_64"
    root = os.path.join(out, name)
    shutil.rmtree(root, ignore_errors=True)
    for d in ("bin", "lib", "plugins", "etc"):
        os.makedirs(os.path.join(root, d))

    app = os.path.join(root, "bin", "Cadjitsu")
    shutil.copy2(os.path.join(build, "src", "app", "Cadjitsu"), app)
    shutil.copytree(os.path.join(build, "src", "app", "fonts"), os.path.join(root, "bin", "fonts"))
    with open(os.path.join(root, "bin", "qt.conf"), "w") as f:
        f.write("[Paths]\nPrefix = ..\nPlugins = plugins\n")

    qt_plugins = os.path.join(prefix, "lib", "qt6", "plugins")
    for d in PLUGIN_DIRS:
        src = os.path.join(qt_plugins, d)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(root, "plugins", d))
    fonts_conf = os.path.join(prefix, "etc", "fonts")
    if os.path.isdir(fonts_conf):
        shutil.copytree(fonts_conf, os.path.join(root, "etc", "fonts"), symlinks=False)

    # Open Image Denoise loads its devices at run time from next to its own
    # library (they are not linked, so ldd does not list them).
    modules = []
    for fn in sorted(os.listdir(os.path.join(prefix, "lib"))):
        src = os.path.join(prefix, "lib", fn)
        if fn.startswith("libOpenImageDenoise_device_") and ".so" in fn and not os.path.islink(src):
            dest = os.path.join(root, "lib", fn)
            shutil.copy2(src, dest)
            os.chmod(dest, os.stat(dest).st_mode | stat.S_IWUSR)
            modules.append(dest)

    # Libraries from the environment, for the app and every plugin (and theirs).
    todo = [app] + modules + [os.path.join(dp, f) for dp, _, fs in os.walk(os.path.join(root, "plugins"))
                              for f in fs if f.endswith(".so")]
    copied = set()
    # Resolved against the environment (plugins' own RPATHs are relative to where they were).
    resolve = dict(os.environ, LD_LIBRARY_PATH=os.path.join(prefix, "lib"))
    while todo:
        for soname, path in ldd(todo.pop(), resolve).items():
            real = os.path.realpath(path)
            if not real.startswith(prefix + os.sep) or soname in copied:
                continue
            dest = os.path.join(root, "lib", soname)
            shutil.copy2(real, dest)
            os.chmod(dest, os.stat(dest).st_mode | stat.S_IWUSR)
            copied.add(soname)
            todo.append(dest)
    set_rpath(app, "$ORIGIN/../lib")

    launcher = os.path.join(root, "cadjitsu")
    with open(launcher, "w") as f:
        f.write("""#!/bin/sh
# Runs Cadjitsu from this folder with its own libraries and Qt plugins.
here="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$here/plugins"
export FONTCONFIG_PATH="$here/etc/fonts"
exec "$here/bin/Cadjitsu" "$@"
""")
    os.chmod(launcher, 0o755)
    with open(os.path.join(root, "README.txt"), "w") as f:
        f.write(f"Cadjitsu {label} for Linux (x86_64)\n\n"
                "Run ./cadjitsu from this folder.\n\n"
                "It brings its own Qt and OpenCASCADE; the system provides X11 and OpenGL "
                "(on Debian / Ubuntu: sudo apt install libgl1 libegl1 libxkbcommon-x11-0 libxcb-cursor0).\n")

    # Nothing may still come from the environment.
    env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(root, "lib"))
    leaks = []
    for dp, _, fs in os.walk(root):
        for fn in fs:
            p = os.path.join(dp, fn)
            if fn == "Cadjitsu" or fn.endswith(".so") or ".so." in fn:
                leaks += [f"{p}: {s} => {t}" for s, t in ldd(p, env).items() if os.path.realpath(t).startswith(prefix + os.sep)]
    if leaks:
        sys.exit("still loading from the environment:\n" + "\n".join(leaks[:20]))

    tarball = os.path.join(out, name + ".tar.gz")
    with tarfile.open(tarball, "w:gz") as t:
        t.add(root, arcname=name)
    print(f"{tarball}: {len(copied)} libraries, {os.path.getsize(tarball) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()

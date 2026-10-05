#!/usr/bin/env python3
"""Makes a macOS app bundle self-contained after macdeployqt.

macdeployqt copies Qt and the libraries the app links into
Contents/Frameworks, but it can leave references and LC_RPATH entries in the
copied libraries that point back to where they came from (Homebrew). On the
build machine those still load, so a library can end up loaded twice: once
from the bundle and once from Homebrew, each copy with its own globals (OCCT's
run-time type registry, for one), which crashes. On any other Mac they fail to
load at all.

For every Mach-O file in the bundle this script:
  * copies each dependency that lives outside the bundle (other than the
    system's) into Contents/Frameworks and points the reference at the copy;
  * deletes LC_RPATH entries that lead outside the bundle.
Then it checks that every dependency resolves inside the bundle or to the
system, and exits non-zero if one does not. Sign the bundle again afterwards.

Usage: macos_bundle_fix.py path/to/App.app
"""

import os
import shutil
import subprocess
import sys

SYSTEM_PREFIXES = ("/usr/lib/", "/System/")
MACHO_MAGICS = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",  # 32-bit
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",  # 64-bit
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",  # universal
}
IN_FRAMEWORKS = "@executable_path/../Frameworks/"


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def is_macho(path):
    if os.path.islink(path) or not os.path.isfile(path):
        return False
    with open(path, "rb") as f:
        return f.read(4) in MACHO_MAGICS


def macho_files(root):
    for d, _, files in os.walk(root):
        for name in sorted(files):
            p = os.path.join(d, name)
            if is_macho(p):
                yield p


def dylib_id(path):
    lines = [l.strip() for l in run("otool", "-D", path).splitlines()[1:]]
    ids = [l for l in lines if l and not l.endswith(":")]
    return ids[0] if ids else None


def dependencies(path):
    """The install names `path` links to (without its own id)."""
    own = dylib_id(path)
    deps = []
    for line in run("otool", "-L", path).splitlines():
        if not line.startswith("\t"):
            continue  # "file:" or "file (architecture arm64):"
        name = line.strip().split(" (compatibility version")[0].strip()
        if name and name != own and name not in deps:
            deps.append(name)
    return deps


def rpaths(path):
    lines = run("otool", "-l", path).splitlines()
    found = []
    for i, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            for follow in lines[i + 1:i + 4]:
                s = follow.strip()
                if s.startswith("path "):
                    rp = s[len("path "):].rsplit(" (offset", 1)[0]
                    if rp not in found:
                        found.append(rp)
                    break
    return found


class Bundle:
    def __init__(self, app):
        self.app = os.path.realpath(app)
        self.contents = os.path.join(self.app, "Contents")
        self.exe_dir = os.path.join(self.contents, "MacOS")
        self.frameworks = os.path.join(self.contents, "Frameworks")
        name = os.path.splitext(os.path.basename(self.app))[0]
        self.exe = os.path.join(self.exe_dir, name)
        if not is_macho(self.exe):
            sys.exit(f"no executable at {self.exe}")
        # Copies made here -> where they came from (their @loader_path
        # references are relative to that).
        self.origin = {}

    def inside(self, path):
        return os.path.realpath(path).startswith(self.app + os.sep)

    def rel(self, path):
        return os.path.relpath(path, self.app)

    def expand(self, ref, owner):
        if ref.startswith("@loader_path/"):
            return os.path.normpath(os.path.join(os.path.dirname(owner), ref[len("@loader_path/"):]))
        if ref.startswith("@executable_path/"):
            return os.path.normpath(os.path.join(self.exe_dir, ref[len("@executable_path/"):]))
        return ref

    def resolve(self, ref, loader, loader_rpaths, exe_rpaths):
        """Where dyld finds `ref` when `loader` asks for it (None: nowhere).
        For a library copied in, a reference that does not resolve from the
        copy is looked up from where the library came from."""
        found = self.resolve_from(ref, loader, loader_rpaths, exe_rpaths)
        if not found and loader in self.origin:
            found = self.resolve_from(ref, self.origin[loader], loader_rpaths, exe_rpaths)
        return found

    def resolve_from(self, ref, loader, loader_rpaths, exe_rpaths):
        if ref.startswith("@rpath/"):
            rest = ref[len("@rpath/"):]
            # The loader's own run paths first, then the executable's.
            for rp, owner in [(r, loader) for r in loader_rpaths] + [(r, self.exe) for r in exe_rpaths]:
                cand = os.path.join(self.expand(rp, owner), rest)
                if os.path.exists(cand):
                    return os.path.normpath(cand)
            return None
        p = self.expand(ref, loader)
        return p if os.path.exists(p) else None

    def bring_in(self, ref, target):
        """Copies what `ref` names into Frameworks (unless it is there
        already); returns (new reference, newly copied binary or None)."""
        marker = ".framework/"
        if marker in ref:
            at = ref.index(marker)
            fw = ref[ref.rfind("/", 0, at) + 1:at + len(".framework")]  # Foo.framework
            inner = ref[at + len(marker):]                               # Versions/A/Foo
            new_ref = IN_FRAMEWORKS + fw + "/" + inner
            dest = os.path.join(self.frameworks, fw, inner)
            if os.path.exists(dest):
                return new_ref, None
            if not target or marker not in target:
                return None, None
            src = target[:target.index(marker) + len(".framework")]
            shutil.copytree(src, os.path.join(self.frameworks, fw), symlinks=True, dirs_exist_ok=True)
            self.origin[dest] = os.path.join(src, inner)
        else:
            name = os.path.basename(ref)
            new_ref = IN_FRAMEWORKS + name
            dest = os.path.join(self.frameworks, name)
            if os.path.exists(dest):
                return new_ref, None
            if not target:
                return None, None
            shutil.copy2(os.path.realpath(target), dest)
            self.origin[dest] = target
        os.chmod(dest, 0o755)
        run("install_name_tool", "-id", new_ref, dest)
        return new_ref, dest

    def add_runtime_modules(self):
        """Open Image Denoise loads its devices at run time from next to its own
        library; they are not linked, so nothing else brings them in."""
        if not any(n.startswith("libOpenImageDenoise") for n in os.listdir(self.frameworks)):
            return
        prefixes = [os.environ.get("HOMEBREW_PREFIX", ""), "/opt/homebrew", "/usr/local"]
        for prefix in [p for p in prefixes if p]:
            lib = os.path.join(prefix, "lib")
            if not os.path.isdir(lib):
                continue
            found = [n for n in sorted(os.listdir(lib)) if n.startswith("libOpenImageDenoise_device_") and n.endswith(".dylib")]
            first = {}  # real file -> the name it was copied as (other names link to it)
            for n in found:
                dest = os.path.join(self.frameworks, n)
                if os.path.exists(dest):
                    continue
                real = os.path.realpath(os.path.join(lib, n))
                if real in first:
                    os.symlink(first[real], dest)
                else:
                    shutil.copy2(real, dest)
                    os.chmod(dest, 0o755)
                    self.origin[dest] = real
                    run("install_name_tool", "-id", IN_FRAMEWORKS + n, dest)
                    first[real] = n
                print(f"  denoiser device: {self.rel(dest)}")
            if found:
                return

    def fix(self):
        os.makedirs(self.frameworks, exist_ok=True)
        self.add_runtime_modules()
        exe_rpaths = rpaths(self.exe)
        queue = list(macho_files(self.contents))
        done = set()
        changed = copied = dropped = 0
        errors = []
        while queue:
            f = queue.pop(0)
            if f in done:
                continue
            done.add(f)
            own_rpaths = rpaths(f)
            for ref in dependencies(f):
                if ref.startswith(SYSTEM_PREFIXES):
                    continue
                target = self.resolve(ref, f, own_rpaths, exe_rpaths)
                if target and self.inside(target):
                    continue
                new_ref, new_file = self.bring_in(ref, target)
                if not new_ref:
                    errors.append(f"{self.rel(f)}: {ref} is not found")
                    continue
                print(f"  {self.rel(f)}: {ref}" + (f" ({target})" if target and target != ref else "") + f" -> {new_ref}")
                run("install_name_tool", "-change", ref, new_ref, f)
                changed += 1
                if new_file:
                    copied += 1
                    print(f"    copied into the bundle: {self.rel(new_file)}")
                    queue.append(new_file)
            for rp in own_rpaths:
                if not rp.startswith("@") and not self.inside(rp):
                    print(f"  {self.rel(f)}: run path {rp} removed")
                    run("install_name_tool", "-delete_rpath", rp, f)
                    dropped += 1
            # A library's own name pointing outside (harmless to dyld, but
            # nothing should name Homebrew).
            own = dylib_id(f)
            if own and own.startswith("/") and not own.startswith(SYSTEM_PREFIXES) and not self.inside(own):
                run("install_name_tool", "-id", IN_FRAMEWORKS + os.path.relpath(f, self.frameworks), f)
        print(f"{changed} references pointed into the bundle, {copied} libraries copied, {dropped} run paths removed")
        return errors

    def verify(self):
        """Every dependency must resolve inside the bundle or to the system."""
        errors = []
        exe_rpaths = rpaths(self.exe)
        for f in macho_files(self.contents):
            own_rpaths = rpaths(f)
            for rp in own_rpaths:
                if not rp.startswith("@") and not self.inside(rp):
                    errors.append(f"{self.rel(f)}: run path outside the bundle: {rp}")
            for ref in dependencies(f):
                if ref.startswith(SYSTEM_PREFIXES):
                    continue
                target = self.resolve(ref, f, own_rpaths, exe_rpaths)
                if not target:
                    errors.append(f"{self.rel(f)}: {ref} does not resolve")
                elif not self.inside(target):
                    errors.append(f"{self.rel(f)}: {ref} resolves outside the bundle ({target})")
        return errors


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    bundle = Bundle(sys.argv[1])
    errors = bundle.fix() + bundle.verify()
    for e in errors:
        print("ERROR:", e)
    if errors:
        sys.exit(1)
    print(f"{bundle.rel(bundle.exe)} and everything it loads come from the bundle or the system")


if __name__ == "__main__":
    main()

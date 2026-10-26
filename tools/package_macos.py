#!/usr/bin/env python3
"""Package the native executable and its non-system dylibs into an ad-hoc signed app."""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True)


def dependencies(binary):
    return [line.strip().split(" (", 1)[0] for line in run("otool", "-L", str(binary)).splitlines()[1:]]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    executable = build / "renut"
    if not executable.is_file():
        raise SystemExit(f"Build the game first: {executable}")
    if "arm64" not in run("lipo", "-archs", str(executable)):
        raise SystemExit("Expected an ARM64 executable")
    app = build / "reNut.app"
    # Recreate generated bundles: replacing Mach-O files in place can retain
    # stale mapped code on macOS and leaves removed dependencies in the app.
    if app.exists():
        shutil.rmtree(app)
    macos = app / "Contents/MacOS"
    frameworks = app / "Contents/Frameworks"
    macos.mkdir(parents=True, exist_ok=True)
    frameworks.mkdir(parents=True, exist_ok=True)
    shutil.copy2(executable, macos / "renut")
    shutil.copy2(build / "renut_controller_check", macos / "renut_controller_check")
    pending = [(executable, macos / "renut")]
    seen = set()
    sdl = build / "mac-sdl/lib/libSDL3.0.dylib"
    if not sdl.is_file():
        raise SystemExit("Build the macOS SDL controller runtime first")
    shutil.copy2(sdl, frameworks / sdl.name)
    seen.add(sdl.name)
    pending.append((sdl, frameworks / sdl.name))
    resources = app / "Contents/Resources/ThirdPartyLicenses"
    shutil.copytree(build / "mac-sdl/licenses", resources, dirs_exist_ok=True)
    while pending:
        original, copied = pending.pop()
        for dependency in dependencies(original):
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            name = Path(dependency).name
            if name == original.name:  # dylib install name
                continue
            source = Path(dependency.replace("@loader_path", str(original.parent)))
            if not source.is_file():
                source = build / name
            if not source.is_file():
                raise RuntimeError(f"Missing runtime dependency {dependency}; expected {source}")
            target = frameworks / name
            if name not in seen:
                shutil.copy2(source, target)
                seen.add(name)
                pending.append((source, target))
            subprocess.run(["install_name_tool", "-change", dependency,
                f"@executable_path/../Frameworks/{name}", str(copied)], check=True)
        if copied.suffix == ".dylib":
            subprocess.run(["install_name_tool", "-id", f"@rpath/{copied.name}", str(copied)], check=True)
    with (app / "Contents/Info.plist").open("wb") as stream:
        plistlib.dump({"CFBundleExecutable": "renut", "CFBundleIdentifier": "io.github.ricjo4w.renut",
            "CFBundleName": "reNut", "CFBundlePackageType": "APPL", "CFBundleVersion": "0.1",
            "CFBundleShortVersionString": "0.1", "LSMinimumSystemVersion": "14.0",
            "NSHighResolutionCapable": True}, stream)
    for library in frameworks.glob("*.dylib"):
        subprocess.run(["codesign", "--force", "--sign", "-", str(library)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", str(macos / "renut_controller_check")], check=True)
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    archive = build / "renut-macos-arm64.zip"
    subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive)], check=True)
    print(f"Built {app}\nPackaged {archive}")


if __name__ == "__main__":
    main()

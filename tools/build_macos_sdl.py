#!/usr/bin/env python3
"""Build the pinned SDL dynapi replacement and libusb for wired Xbox on macOS."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

SDL_COMMIT = 'd0183f0ee3b2497f49c6322c62354d114c22af29'

def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)

def unpack(root, name, url, digest, folder):
    archive = root / name
    if not archive.exists():
        temporary = archive.with_suffix('.download')
        run('curl', '--fail', '--location', '--retry', '3', url, '-o', str(temporary))
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'Checksum mismatch: {archive}')
    source = root / folder
    if not source.exists():
        with tarfile.open(archive) as stream:
            stream.extractall(root, filter='data')
    return source

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--prefix', type=Path, required=True)
    p.add_argument('--jobs', default='4')
    a = p.parse_args()
    prefix = a.prefix.resolve()
    recipe = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    stamp = prefix / 'recipe.sha256'
    if stamp.exists() and stamp.read_text() == recipe and all(
            (prefix / 'lib' / name).is_file()
            for name in ('libSDL3.0.dylib', 'libusb-1.0.0.dylib')):
        return
    root = prefix.parent / 'mac-sdl-source'
    root.mkdir(parents=True, exist_ok=True)
    usb = unpack(root, 'libusb.tar.bz2',
        'https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2',
        'fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf', 'libusb-1.0.30')
    sdl = unpack(root, 'sdl.tar.gz', f'https://github.com/libsdl-org/SDL/archive/{SDL_COMMIT}.tar.gz',
        'bf9aa0dede063a72bff3f2d79cb63c6921d7aa8e8dbd138e1712f51576373d05', f'SDL-{SDL_COMMIT}')
    usbbuild = root / 'usb-build'
    usbbuild.mkdir(exist_ok=True)
    if not (usbbuild / 'Makefile').exists():
        import os
        env = dict(os.environ, MACOSX_DEPLOYMENT_TARGET='14.0', CC='/usr/bin/clang',
                   CFLAGS='-O2 -arch arm64 -mmacosx-version-min=14.0')
        run(str(usb / 'configure'), f'--prefix={prefix}', '--disable-static', cwd=usbbuild, env=env)
    run('make', '-j' + a.jobs, cwd=usbbuild)
    run('make', 'install', cwd=usbbuild)
    # The SDK contains an older static SDL. Give this dynapi replacement's ObjC
    # classes private names so the Objective-C runtime never selects the old ones.
    names = set()
    for file in (sdl / 'src').rglob('*'):
        if file.suffix in ('.h', '.m'):
            names.update(re.findall(r'@interface\s+((?:SDL\w*|Cocoa_\w+))\s*:', file.read_text()))
    header = root / 'renut_sdl_namespace.h'
    content = ''.join(f'#define {name} Renut_{name}\n' for name in sorted(names))
    if not header.exists() or header.read_text() != content:
        header.write_text(content)
    build = root / 'sdl-build'
    run('cmake', '-S', str(sdl), '-B', str(build), '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0', f'-DCMAKE_INSTALL_PREFIX={prefix}',
        f'-DCMAKE_C_FLAGS=-include "{header}"', '-DSDL_SHARED=ON', '-DSDL_STATIC=OFF',
        '-DSDL_TESTS=OFF', '-DSDL_TEST_LIBRARY=OFF', '-DSDL_EXAMPLES=OFF',
        '-DSDL_HIDAPI_LIBUSB=ON', '-DSDL_HIDAPI_LIBUSB_SHARED=OFF',
        f'-DLibUSB_LIBRARY={prefix}/lib/libusb-1.0.0.dylib',
        f'-DLibUSB_INCLUDE_PATH={prefix}/include/libusb-1.0',
        f'-DSDL_VENDOR_INFO=reNut-{SDL_COMMIT[:12]}')
    run('cmake', '--build', str(build), '--parallel', a.jobs)
    run('cmake', '--install', str(build))
    licenses = prefix / 'licenses'
    licenses.mkdir(exist_ok=True)
    shutil.copy2(sdl / 'LICENSE.txt', licenses / 'SDL.txt')
    shutil.copy2(usb / 'COPYING', licenses / 'libusb.txt')
    stamp.write_text(recipe)

if __name__ == '__main__':
    main()

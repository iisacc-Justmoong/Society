#!/usr/bin/env python3
"""Build the pinned OpenSSL 3 runtime required by Qt Android HTTPS (Python 3.12+)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

PRODUCT = Path(__file__).resolve().parents[1]
VERSION = '3.5.8'
SHA256 = 'a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2'
URL = f'https://github.com/openssl/openssl/releases/download/openssl-{VERSION}/openssl-{VERSION}.tar.gz'
TARGETS = {'arm64-v8a': 'android-arm64', 'armeabi-v7a': 'android-arm',
           'x86_64': 'android-x86_64', 'x86': 'android-x86'}


def verify_archive(archive):
    with archive.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    if actual != SHA256:
        raise ValueError(f'OpenSSL archive checksum mismatch: {archive}')


def patch_qt_sonames(source):
    # Qt loads libssl_3.so/libcrypto_3.so. Keep upstream OPENSSL_3.x symbol
    # versions while giving the Android libraries the Qt filename suffix.
    configuration = source / 'Configurations/15-android.conf'
    text = configuration.read_text()
    marker = '        shared_extension => ".so",'
    if text.count(marker) != 1:
        raise ValueError('Unexpected OpenSSL Android configuration')
    configuration.write_text(text.replace(marker, marker + '\n        shlib_variant => "_3",'))
    definitions = source / 'util/mkdef.pl'
    text = definitions.read_text()
    if text.count('OPENSSL${SO_VARIANT}_') != 2:
        raise ValueError('Unexpected OpenSSL symbol version generator')
    definitions.write_text(text.replace('OPENSSL${SO_VARIANT}_', 'OPENSSL_'))


def build_openssl(ndk, abi, jobs=4):
    ndk = ndk.resolve(strict=True)
    root = PRODUCT / 'build/android'
    directory = root / f'openssl-{VERSION}' / abi
    output = directory / 'lib'
    stamp = directory / 'build.json'
    configuration = {'version': VERSION, 'sha256': SHA256, 'abi': abi, 'api': 28,
                     'ndk': str(ndk), 'ndkRevision': (ndk / 'source.properties').read_text(),
                     'builder': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    libraries = ('libcrypto_3.so', 'libssl_3.so')
    if stamp.is_file() and json.loads(stamp.read_text()) == configuration and all(
            (output / name).is_file() for name in libraries):
        return output
    root.mkdir(parents=True, exist_ok=True)
    archive = root / f'openssl-{VERSION}.tar.gz'
    if not archive.is_file():
        partial = archive.with_suffix('.partial')
        urllib.request.urlretrieve(URL, partial)
        verify_archive(partial)
        partial.replace(archive)
    verify_archive(archive)
    source = directory / f'openssl-{VERSION}'
    if source.exists():
        shutil.rmtree(source)
    directory.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as package:
        package.extractall(directory, filter='data')
    patch_qt_sonames(source)
    host = 'darwin-x86_64' if platform.system() == 'Darwin' else 'linux-x86_64'
    toolchain = ndk / 'toolchains/llvm/prebuilt' / host / 'bin'
    if not (toolchain / 'clang').is_file():
        raise ValueError(f'Missing Android NDK toolchain: {toolchain}')
    env = os.environ.copy()
    for key in ('CC', 'CXX', 'AR', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CPATH',
                'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH'):
        env.pop(key, None)
    env.update(ANDROID_NDK_ROOT=str(ndk), ANDROID_NDK_HOME=str(ndk),
               PATH=str(toolchain) + os.pathsep + env.get('PATH', ''))
    commands = [
        ['perl', 'Configure', 'shared', TARGETS[abi], '-D__ANDROID_API__=28',
         'no-tests', 'no-apps', '-Wl,-z,max-page-size=16384', '-Wl,-z,common-page-size=16384'],
        ['make', f'-j{jobs}', 'build_libs'],
    ]
    with (directory / 'build.log').open('w') as log:
        for command in commands:
            subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    output.mkdir(parents=True, exist_ok=True)
    for name in libraries:
        shutil.copy2(source / name, output / name)
        subprocess.run([str(toolchain / 'llvm-strip'), '--strip-unneeded', str(output / name)], check=True)
    shutil.copy2(source / 'LICENSE.txt', output / 'LICENSE.txt')
    stamp.write_text(json.dumps(configuration, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', required=True, type=Path)
    parser.add_argument('--abi', choices=TARGETS, default='arm64-v8a')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    print(build_openssl(args.ndk, args.abi, args.jobs))

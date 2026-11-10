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
import sys
import tarfile
import urllib.request

PRODUCT = Path(__file__).resolve().parents[1]
VERSION = '3.5.8'
SHA256 = 'a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2'
URL = f'https://github.com/openssl/openssl/releases/download/openssl-{VERSION}/openssl-{VERSION}.tar.gz'
TARGETS = {'arm64-v8a': 'android-arm64', 'armeabi-v7a': 'android-arm',
           'x86_64': 'android-x86_64', 'x86': 'android-x86'}


def file_provider():
    directory = os.environ.get('IIFILEPROVIDER_PYTHON_DIRECTORY') or str(
        Path.home() / '.local/SDK/iiFileProvider/share/iiFileProvider/python')
    if directory not in sys.path:
        sys.path.insert(0, directory)
    import ii_file_provider
    return ii_file_provider


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
    file_provider().write_text(configuration, text.replace(marker, marker + '\n        shlib_variant => "_3",'))
    definitions = source / 'util/mkdef.pl'
    text = definitions.read_text()
    if text.count('OPENSSL${SO_VARIANT}_') != 2:
        raise ValueError('Unexpected OpenSSL symbol version generator')
    file_provider().write_text(definitions, text.replace('OPENSSL${SO_VARIANT}_', 'OPENSSL_'))


def build_openssl(ndk, abi, jobs=4):
    if abi not in TARGETS or jobs < 1:
        raise ValueError('A supported Android ABI and positive job count are required')
    ndk = ndk.resolve(strict=True)
    root = PRODUCT / 'build/android'
    directory = root / f'openssl-{VERSION}' / abi
    output = directory / 'lib'
    resources = PRODUCT / 'resources'
    dependency_root = resources / 'dependencies/android-openssl'
    state = resources / '.state/android-openssl'
    stamp = state / abi / 'build.json'
    headers = directory / 'include/openssl'
    for path in (PRODUCT / 'build', root, root / f'openssl-{VERSION}', directory, output,
                 directory / 'include', headers, resources, resources / 'dependencies',
                 dependency_root, resources / '.state', state, state / abi, stamp):
        if path.is_symlink():
            raise ValueError('OpenSSL build and resource paths cannot be symlinks')
    configuration = {'version': VERSION, 'sha256': SHA256, 'abi': abi, 'api': 28,
                     'ndk': str(ndk), 'ndkRevision': (ndk / 'source.properties').read_text(),
                     'builder': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    libraries = ('libcrypto_3.so', 'libssl_3.so')
    if stamp.is_file() and json.loads(stamp.read_text()) == configuration and all(
            (output / name).is_file() for name in libraries) and all(
            (directory / 'include/openssl' / name).is_file() for name in ('evp.h', 'configuration.h', 'opensslconf.h')):
        return output
    files = file_provider()
    files.mkdir(dependency_root, parents=True, exist_ok=True)
    files.mkdir(state / abi, parents=True, exist_ok=True)
    files.mkdir(directory, parents=True, exist_ok=True)
    if stamp.exists():
        stamp.unlink()
    archive = dependency_root / f'openssl-{VERSION}.tar.gz'
    if archive.is_symlink():
        raise ValueError('OpenSSL archive cannot be a symlink')
    if not archive.is_file():
        with files.staged_path(archive, overwrite=False) as partial:
            with urllib.request.urlopen(URL) as response, files.open(partial, 'wb') as stream:
                shutil.copyfileobj(response, stream)
            verify_archive(partial)
    verify_archive(archive)
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
    with files.TemporaryDirectory(prefix='source-', dir=state) as temporary_source, \
         files.TemporaryDirectory(prefix='work-', dir=directory) as temporary_build:
        source_root = Path(temporary_source)
        work = Path(temporary_build)
        temporary = source_root / 'tmp'
        files.mkdir(temporary)
        env.update(TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
        with tarfile.open(archive) as package:
            package.extractall(source_root, filter='data')
        source = source_root / f'openssl-{VERSION}'
        if source.is_symlink() or not source.is_dir():
            raise ValueError('OpenSSL archive has an unexpected source root')
        patch_qt_sonames(source)
        commands = [
            ['perl', str(source / 'Configure'), 'shared', TARGETS[abi], '-D__ANDROID_API__=28',
             'no-tests', 'no-apps', '-Wl,-z,max-page-size=16384', '-Wl,-z,common-page-size=16384'],
            ['make', f'-j{jobs}', 'build_libs'],
        ]
        for command in commands:
            subprocess.run(command, cwd=work, env=env, check=True)
        files.mkdir(output, parents=True, exist_ok=True)
        for name in libraries:
            files.copy2(work / name, output / name)
            subprocess.run([str(toolchain / 'llvm-strip'), '--strip-unneeded', str(output / name)], check=True)
        files.copy2(source / 'LICENSE.txt', output / 'LICENSE.txt')
        files.copytree(source / 'include/openssl', headers, dirs_exist_ok=True)
        generated = work / 'include/openssl'
        if generated.is_dir():
            files.copytree(generated, headers, dirs_exist_ok=True)
        if not all((headers / name).is_file() for name in ('evp.h', 'configuration.h', 'opensslconf.h')):
            raise RuntimeError('OpenSSL build did not produce matching public headers')
        files.write_text(stamp, json.dumps(configuration, indent=2) + '\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', required=True, type=Path)
    parser.add_argument('--abi', choices=TARGETS, default='arm64-v8a')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    print(build_openssl(args.ndk, args.abi, args.jobs))

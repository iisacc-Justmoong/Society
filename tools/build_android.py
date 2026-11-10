#!/usr/bin/env python3
"""Build Android Society and its account, hosting and storage SDKs under build/android/."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
from build_android_openssl import build_openssl, TARGETS


def file_provider():
    directory = os.environ.get('IIFILEPROVIDER_PYTHON_DIRECTORY') or str(
        Path.home() / '.local/SDK/iiFileProvider/share/iiFileProvider/python')
    if directory not in sys.path:
        sys.path.insert(0, directory)
    import ii_file_provider
    return ii_file_provider


def build_environment(base, state, sdk, ndk):
    env = dict(base)
    for key in ('CPATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'CMAKE_PREFIX_PATH'):
        env.pop(key, None)
    temporary = state / 'tmp'
    env.update(ANDROID_SDK_ROOT=str(sdk), ANDROID_HOME=str(sdk), ANDROID_NDK_ROOT=str(ndk),
               GRADLE_USER_HOME=str(state / 'gradle'), ANDROID_USER_HOME=str(state / 'user'),
               TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    env['ORG_GRADLE_PROJECT_android.builder.sdkDownload'] = 'false'
    env['GRADLE_OPTS'] = (env.get('GRADLE_OPTS', '') +
                          ' -Dorg.gradle.problems.report=false').strip()
    java_temp = '"-Djava.io.tmpdir=' + str(temporary).replace('"', '\\"') + '"'
    env['JAVA_TOOL_OPTIONS'] = (env.get('JAVA_TOOL_OPTIONS', '') + ' ' + java_temp).strip()
    return env


def dependency_prefix(source, abi, name):
    return source / 'resources/dependencies/android' / abi / name


def main(argv=None, project=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt', required=True, type=Path, help='Qt 6.8.3 Android ABI prefix')
    parser.add_argument('--qt-host', required=True, type=Path, help='Matching Qt 6.8.3 host tools prefix')
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--ndk', required=True, type=Path, help='Qt 6.8 NDK r27c or r26b')
    parser.add_argument('--lvrs', required=True, type=Path, help='Installed LVRS prefix for the same Android ABI')
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--abi', choices=TARGETS, default='arm64-v8a', help='ABI matching the Qt and LVRS prefixes')
    parser.add_argument('--java-home', type=Path, help='JDK 17 or 21 home compatible with the Qt Gradle version')
    parser.add_argument('--package', action='store_true', help='Generate an unsigned Release APK; signing is separate')
    parser.add_argument('--account-runtime-probe', action='store_true', help='Enable the credential-free native TLS/UI verification build')
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    source = Path(project).resolve(strict=True) if project is not None else Path(__file__).resolve().parents[1]
    workspace = source.parents[1]
    build = source / 'build/android'
    files = file_provider()
    for path in (source / 'build', build, source / 'resources', source / 'resources/dependencies',
                 source / 'resources/dependencies/android', source / 'resources/dependencies/android' / args.abi):
        if path.is_symlink():
            raise ValueError('Android build and dependency paths cannot be symlinks')
    files.mkdir(build, parents=True, exist_ok=True)
    qt, sdk, ndk, lvrs = (value.resolve(strict=True) for value in (args.qt, args.sdk, args.ndk, args.lvrs))
    common = [str(qt / 'bin/qt-cmake'), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF',
              f'-DQT_HOST_PATH={args.qt_host.resolve(strict=True)}',
              f'-DANDROID_SDK_ROOT={sdk}', f'-DANDROID_NDK_ROOT={ndk}', f'-DCMAKE_ANDROID_NDK={ndk}',
              f'-DQT_CHAINLOAD_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake',
              '-DANDROID_PLATFORM=android-28', f'-DANDROID_ABI={args.abi}']

    state_root = source / 'resources/.state'
    if (source / 'resources').is_symlink() or state_root.is_symlink():
        raise ValueError('Android build state cannot be a symlink')
    files.mkdir(state_root, parents=True, exist_ok=True)
    with files.TemporaryDirectory(prefix='android-build-', dir=state_root) as owner:
        state = Path(owner)
        for name in ('gradle', 'user', 'tmp'):
            files.mkdir(state / name)
        env = build_environment(os.environ, state, sdk, ndk)
        if args.java_home:
            java_home = args.java_home.resolve(strict=True)
            if not (java_home / 'bin/java').is_file():
                parser.error('--java-home must contain bin/java')
            env['JAVA_HOME'] = str(java_home)
            env['PATH'] = str(java_home / 'bin') + os.pathsep + env.get('PATH', '')

        def run(command, label):
            print(label, flush=True)
            result = subprocess.run(command, env=env)
            if result.returncode:
                raise SystemExit(result.returncode)

        packages = {}
        for name in ('iiAccountManager', 'iiWebServer', 'iiSocietyContainer', 'iiSocietyHelper', 'iiPhotoLibrary'):
            directory = workspace / 'SDK' / name / 'build/society-android'
            prefix = dependency_prefix(source, args.abi, name)
            if directory.is_symlink() or directory.parent.is_symlink() or prefix.is_symlink():
                raise ValueError('Android module build and dependency paths cannot be symlinks')
            definitions = [f'-D{key}_DIR={value}' for key, value in packages.items()]
            if name == 'iiAccountManager':
                definitions += ['-DIIACCOUNTMANAGER_BUILD_QUICK=ON', f'-DLVRS_DIR={lvrs}/lib/cmake/LVRS']
            run(common + ['-S', str(workspace / 'SDK' / name), '-B', str(directory), f'-DCMAKE_INSTALL_PREFIX={prefix}'] + definitions, name + '-configure')
            run(['cmake', '--build', str(directory), '--parallel', str(args.jobs)], name + '-build')
            run(['cmake', '--install', str(directory)], name + '-install')
            packages[name] = prefix / 'lib/cmake' / name
        directory = build / 'app'
        print('Building Qt Android OpenSSL runtime', flush=True)
        openssl = build_openssl(ndk, args.abi, args.jobs)
        run(common + ['-S', str(source), '-B', str(directory), f'-DLVRS_DIR={lvrs}/lib/cmake/LVRS',
                      f'-DSOCIETY_ANDROID_OPENSSL_DIR={openssl}',
                      f'-DSOCIETY_ACCOUNT_RUNTIME_PROBE={"ON" if args.account_runtime_probe else "OFF"}']
            + [f'-D{key}_DIR={value}' for key, value in packages.items()], 'app-configure')
        run(['cmake', '--build', str(directory), '--parallel', str(args.jobs)], 'app-build')
        if args.package:
            run(['cmake', '--build', str(directory), '--target', 'apk', '--parallel', str(args.jobs)], 'app-apk')
        print(f'Android Society build: {directory}')


if __name__ == '__main__':
    main()

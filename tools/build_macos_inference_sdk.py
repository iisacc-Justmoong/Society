"""Build Society's native image runtime without the optional LibTorch backend."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def normalize_pkgconfig_prefix(prefix, files):
    """Make the bundled diffusion package independent of the staging path."""
    path = prefix / 'lib/pkgconfig/stable-diffusion.pc'
    if any(part.is_symlink() for part in (prefix, prefix / 'lib', path.parent, path)):
        raise ValueError('The diffusion pkg-config path cannot contain symlinks')
    if not path.exists():
        return
    lines = path.read_text(encoding='utf-8').splitlines(keepends=True)
    replacements = {'prefix': '${pcfiledir}/../..', 'libdir': '${prefix}/lib',
                    'includedir': '${prefix}/include'}
    for key, value in replacements.items():
        matches = [index for index, line in enumerate(lines) if line.startswith(key + '=')]
        if len(matches) != 1:
            raise ValueError('Invalid diffusion pkg-config entry: ' + key)
        lines[matches[0]] = key + '=' + value + '\n'
    files.write_text(path, ''.join(lines))


def build_plan(source, prefix, parallel):
    build = source / 'build/society-macos'
    return [
        ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
         '-DIILD_ENABLE_NATIVE_DIFFUSION=ON', '-DIILD_ENABLE_LIBTORCH=OFF',
         '-DIILD_ENABLE_MLX=ON', '-DIILD_ENABLE_COREML=ON', '-DIILD_ENABLE_POSE=ON',
         '-DIILD_INSTALL_PYTHON_REFERENCE=OFF', '-DIILD_BUILD_TOOLS=OFF',
         '-DBUILD_TESTING=ON', '-DPython3_EXECUTABLE=' + sys.executable],
        ['cmake', '--build', str(build), '--parallel', str(parallel), '--target',
         'iiLocalDiffusion', 'iiModelPackaging',
         'NativeDiffusionTests', 'NativeResultTests', 'ModelManifestTests'],
        ['ctest', '--test-dir', str(build), '--output-on-failure', '-R',
         '^(NativeDiffusionTests|NativeResultTests|ModelManifestTests)$'],
        ['cmake', '--install', str(build)],
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-source', type=Path, default=ROOT.parents[1] / 'SDK/iiLocalDiffusion')
    parser.add_argument('--parallel', type=int, default=6)
    args = parser.parse_args()
    if args.parallel < 1:
        parser.error('--parallel must be positive')
    if sys.platform != 'darwin':
        parser.error('This profile targets the macOS Society host.')
    source = args.sdk_source.resolve()
    if not (source / 'CMakeLists.txt').is_file():
        parser.error('Set --sdk-source to the iiLocalDiffusion source checkout.')
    if not source.is_relative_to(ROOT.parents[1]):
        parser.error('The SDK source and its build directory must stay inside Workspace.')
    prefix = ROOT / 'resources/dependencies/macos-runtime/iiLocalDiffusion-apple'
    for path in (source / 'build', source / 'build/society-macos', ROOT / 'resources',
                 ROOT / 'resources/dependencies', ROOT / 'resources/dependencies/macos-runtime', prefix):
        if path.is_symlink():
            raise ValueError('Inference build and dependency paths cannot be symlinks')
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('DYLD_', 'QML', 'QT_', 'QSG_'))}
    provider_dir = os.environ.get('IIFILEPROVIDER_PYTHON_DIRECTORY') or str(
        Path.home() / '.local/SDK/iiFileProvider/share/iiFileProvider/python')
    if provider_dir not in sys.path:
        sys.path.insert(0, provider_dir)
    import ii_file_provider as files
    state = ROOT / 'resources/.state'
    if state.is_symlink():
        raise ValueError('Inference build state cannot be a symlink')
    files.mkdir(state, parents=True, exist_ok=True)
    with files.TemporaryDirectory(prefix='macos-inference-', dir=state) as owner:
        environment.update(TMPDIR=owner, TMP=owner, TEMP=owner)
        for command in build_plan(source, prefix, args.parallel):
            print(' '.join(command), flush=True)
            subprocess.run(command, env=environment, check=True)
    normalize_pkgconfig_prefix(prefix, files)
    library = prefix / 'lib/libiiLocalDiffusion.dylib'
    linked = subprocess.check_output(['otool', '-L', str(library)], text=True)
    if any(name in linked.lower() for name in ('libtorch', 'libc10')):
        raise RuntimeError('The Society inference profile must not load LibTorch.')
    result = {'sdkSource': str(source), 'package': str(prefix / 'lib/cmake/iiLocalDiffusion'),
              'nativeImageGeneration': True, 'mlx': True, 'coreml': True, 'pose': True,
              'libtorch': False, 'libraryDependencies': linked}
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()

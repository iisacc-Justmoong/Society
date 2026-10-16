"""Build Society's native image runtime without the optional LibTorch backend."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-source', type=Path, default=ROOT.parents[1] / 'SDK/iiLocalDiffusion')
    parser.add_argument('--parallel', type=int, default=6)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This profile targets the macOS Society host.')
    source = args.sdk_source.resolve()
    if not (source / 'CMakeLists.txt').is_file():
        parser.error('Set --sdk-source to the iiLocalDiffusion source checkout.')
    root = ROOT / 'build/runtime-sdk/iiLocalDiffusion'
    build, prefix = root / 'build', root / 'install'
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('DYLD_', 'QML', 'QT_', 'QSG_'))}
    commands = [
        ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
         '-DIILD_ENABLE_NATIVE_DIFFUSION=ON', '-DIILD_ENABLE_LIBTORCH=OFF',
         '-DIILD_ENABLE_MLX=ON', '-DIILD_ENABLE_COREML=ON', '-DIILD_ENABLE_POSE=ON',
         '-DIILD_INSTALL_PYTHON_REFERENCE=OFF', '-DIILD_BUILD_TOOLS=ON',
         '-DBUILD_TESTING=ON', '-DPython3_EXECUTABLE=' + sys.executable],
        ['cmake', '--build', str(build), '--parallel', str(args.parallel), '--target',
         'iiLocalDiffusion', 'iiModelPackaging', 'iild-run', 'iild-model-package',
         'NativeDiffusionTests', 'NativeResultTests', 'ModelManifestTests'],
        ['ctest', '--test-dir', str(build), '--output-on-failure', '-R',
         '^(NativeDiffusionTests|NativeResultTests|ModelManifestTests)$'],
        ['cmake', '--install', str(build)],
    ]
    root.mkdir(parents=True, exist_ok=True)
    for command in commands:
        print(' '.join(command), flush=True)
        subprocess.run(command, env=environment, check=True)
    library = prefix / 'lib/libiiLocalDiffusion.0.6.1.dylib'
    linked = subprocess.check_output(['otool', '-L', str(library)], text=True)
    if any(name in linked.lower() for name in ('libtorch', 'libc10')):
        raise RuntimeError('The Society inference profile must not load LibTorch.')
    result = {'sdkSource': str(source), 'package': str(prefix / 'lib/cmake/iiLocalDiffusion'),
              'nativeImageGeneration': True, 'mlx': True, 'coreml': True, 'pose': True,
              'libtorch': False, 'libraryDependencies': linked}
    (root / 'profile.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()

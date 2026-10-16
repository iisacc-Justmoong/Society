"""Validate Society's self-contained macOS runtime and compact release budget."""
import argparse
import json
import os
from pathlib import Path


def inspect(bundle, max_mib=512):
    files, artifacts, total = 0, [], 0
    for current, directories, names in os.walk(bundle, followlinks=False):
        current = Path(current)
        for name in directories:
            if name.endswith('.dSYM') or name == 'designer':
                artifacts.append(str((current / name).relative_to(bundle)))
        directories[:] = [name for name in directories if not (current / name).is_symlink()]
        for name in names:
            path = current / name
            if path.is_symlink():
                continue
            files += 1
            total += path.stat().st_size
            if name.endswith('.qmltypes'):
                artifacts.append(str(path.relative_to(bundle)))
    forbidden = [str(path.relative_to(bundle)) for path in (bundle / 'Contents/Frameworks').iterdir()
                 if path.name.startswith(('libtorch', 'libc10', 'Qt3D', 'QtVirtualKeyboard'))]
    required = ['Contents/MacOS/Society', 'Contents/MacOS/SocietyDaemon',
                'Contents/MacOS/SocietyContainerDaemon', 'Contents/Frameworks/libLVRS.dylib',
                'Contents/Resources/qml/LVRS/qmldir',
                'Contents/Resources/qml/LVRS/qml/ApplicationWindow.qml',
                'Contents/Resources/qml/QtQuick/Controls/Basic/qmldir']
    missing = [name for name in required if not (bundle / name).is_file()]
    if not list((bundle / 'Contents/Frameworks').glob('libiiLocalDiffusion*.dylib')):
        missing.append('native image generation runtime')
    styles = bundle / 'Contents/Resources/qml/QtQuick/Controls'
    wrong_styles = [path.name for path in styles.iterdir()
                    if path.is_dir() and path.name not in ('Basic', 'impl')
                    and (path / 'qmldir').is_file()]
    duplicate_assets = [str(path.relative_to(bundle))
                        for path in (bundle / 'Contents/Resources/qml/LVRS/resources').rglob('*')
                        if path.is_file() and path.suffix.lower() in ('.svg', '.ttf', '.png')]
    return {'bundle': str(bundle), 'fileBytes': total, 'fileCount': files,
            'maxBytes': max_mib * 1024 ** 2, 'developmentArtifacts': artifacts,
            'unneededRuntimes': forbidden, 'missingRequiredFiles': missing,
            'unselectedStyles': wrong_styles,
            'duplicateEmbeddedAssets': duplicate_assets,
            'passed': total <= max_mib * 1024 ** 2 and not any(
                (artifacts, forbidden, missing, wrong_styles, duplicate_assets))}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--max-mib', type=int, default=512)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = inspect(args.bundle.resolve(), args.max_mib)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)

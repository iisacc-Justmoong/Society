#!/usr/bin/env python3
"""Build Society's active SDKs and app for a single iOS ABI, under build/."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PRODUCT = Path(__file__).resolve().parents[1]
WORKSPACE = PRODUCT.parents[1]


def probe(command):
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        return result.stdout.strip() if result.returncode == 0 else None
    except OSError:
        return None


def preflight(qt, sdk, team):
    problems = []
    for program in ('cmake', 'ninja', 'xcrun'):
        if not shutil.which(program):
            problems.append(f'Missing tool: {program}')
    xcode = probe(['xcodebuild', '-version'])
    sdk_path = probe(['xcrun', '--sdk', sdk, '--show-sdk-path'])
    if not xcode or not sdk_path:
        problems.append(f'Full Xcode with {sdk} is required; Command Line Tools cannot build an iOS app.')
    for relative in ('ios/lib/cmake/Qt6/qt.toolchain.cmake', 'macos/lib/cmake/Qt6/Qt6Config.cmake',
                     'ios/plugins/sqldrivers/libqsqlite.a'):
        if not (qt / relative).is_file():
            problems.append(f'Missing Qt 6.8.3 component: {qt / relative}')
    if sdk == 'iphoneos' and not team:
        problems.append('Set SOCIETY_IOS_TEAM to the Apple team that provisions the Society App Group.')
    return {'xcode': xcode, 'sdk': sdk, 'sdkPath': sdk_path, 'problems': problems}


def commands(mode, qt, team, configuration):
    sdk = 'iphoneos' if mode == 'ios-device' else 'iphonesimulator'
    prefix = WORKSPACE / 'build' / mode / 'install'
    common = [f'-DCMAKE_TOOLCHAIN_FILE={qt}/ios/lib/cmake/Qt6/qt.toolchain.cmake',
              f'-DQT_HOST_PATH={qt}/macos', f'-DCMAKE_OSX_SYSROOT={sdk}',
              '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=16.0',
              f'-DCMAKE_BUILD_TYPE={configuration}', '-DBUILD_TESTING=OFF',
              f'-DCMAKE_INSTALL_PREFIX={prefix}', f'-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH={prefix}']
    steps = []
    for name in ('LVRS', 'iiSocietyContainer', 'iiSocietyHelper'):
        source = WORKSPACE / 'SDK' / name
        build = source / 'build' / mode
        options = []
        if name == 'LVRS':
            options = ['-DLVRS_BUILD_SHARED_LIBS=OFF', '-DLVRS_BUILD_EXAMPLES=OFF',
                       '-DLVRS_BUILD_TESTS=OFF', '-DLVRS_ENABLE_FRAMEWORK_BOOTSTRAP_TARGETS=OFF']
        elif name == 'iiSocietyHelper':
            options = [f'-DiiSocietyContainer_DIR={prefix}/lib/cmake/iiSocietyContainer']
        steps.extend([
            ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', *common, *options],
            ['cmake', '--build', str(build), '--parallel', '2'],
            ['cmake', '--install', str(build), '--config', configuration],
        ])
    build = PRODUCT / 'build' / mode
    steps.append(['cmake', '-S', str(PRODUCT), '-B', str(build), '-G', 'Xcode', *common,
                  f'-DLVRS_DIR={prefix}/lib/cmake/LVRS',
                  f'-DiiSocietyContainer_DIR={prefix}/lib/cmake/iiSocietyContainer',
                  f'-DiiSocietyHelper_DIR={prefix}/lib/cmake/iiSocietyHelper',
                  '-DSOCIETY_IOS_APP_GROUP=group.com.iisacc.society', f'-DSOCIETY_IOS_TEAM={team}'])
    steps.append(['cmake', '--build', str(build), '--config', configuration, '--target', 'Society', '--parallel', '2'])
    return steps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('ios-device', 'ios-simulator'), default='ios-simulator')
    parser.add_argument('--qt', type=Path, default=Path('/Volumes/Storage/Qt/6.8.3'))
    parser.add_argument('--team', default=os.environ.get('SOCIETY_IOS_TEAM', ''))
    parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Debug')
    parser.add_argument('--check', action='store_true', help='Report prerequisites without configuring or building')
    args = parser.parse_args()
    sdk = 'iphoneos' if args.platform == 'ios-device' else 'iphonesimulator'
    report = preflight(args.qt.resolve(), sdk, args.team)
    output = PRODUCT / 'build' / args.platform
    output.mkdir(parents=True, exist_ok=True)
    report['commands'] = commands(args.platform, args.qt.resolve(), args.team, args.configuration)
    (output / 'preflight.json').write_text(json.dumps(report, indent=2) + '\n')
    if report['problems']:
        for problem in report['problems']:
            print(problem, file=sys.stderr)
        return 1
    if args.check:
        print(f"Ready: {report['sdkPath']}")
        return 0
    for command in report['commands']:
        subprocess.run(command, cwd=WORKSPACE, check=True)
    print(f'Society {args.platform} built in {output}. Device/Files runtime checks are separate.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

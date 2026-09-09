#!/usr/bin/env python3
"""Check the actual signed device app and its File Provider before installing."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import plistlib
import subprocess


def output(*command):
    return subprocess.check_output(command, stderr=subprocess.DEVNULL)


def verify(app, device):
    group = 'group.com.iisacc.society'
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    extension = app / 'PlugIns' / 'SocietyFileProvider.appex'
    results = []
    for bundle, identifier in ((app, 'com.iisacc.society'),
                               (extension, 'com.iisacc.society.fileprovider')):
        info = plistlib.loads((bundle / 'Info.plist').read_bytes())
        assert info['CFBundleIdentifier'] == identifier
        assert info['CFBundleSupportedPlatforms'] == ['iPhoneOS']
        assert info['SocietyAppGroup'] == group
        executable = bundle / info['CFBundleExecutable']
        if bundle == app:
            assert '_society-pair._udp' in info.get('NSBonjourServices', []), 'Missing Society discovery service declaration'
            assert info.get('NSLocalNetworkUsageDescription'), 'Missing local discovery purpose'
            assert 'QR' in info.get('NSCameraUsageDescription', ''), 'Missing pairing camera purpose'
            assert 'NSMicrophoneUsageDescription' not in info, 'QR pairing must not request microphone access'
            assert '/AVFoundation.framework/' in output('otool', '-L', str(executable)).decode(), 'Missing native QR camera framework'
        assert 'platform IOS\n' in output('xcrun', 'vtool', '-show-build', str(executable)).decode()
        assert 'arm64' in output('lipo', '-archs', str(executable)).decode()
        rights = plistlib.loads(output('codesign', '-d', '--entitlements', ':-', str(bundle)))
        assert rights['com.apple.security.application-groups'] == [group]
        profile = plistlib.loads(output('security', 'cms', '-D', '-i', str(bundle / 'embedded.mobileprovision')))
        assert profile['ExpirationDate'].replace(tzinfo=timezone.utc) > datetime.now(timezone.utc)
        assert device in profile['ProvisionedDevices']
        assert group in profile['Entitlements']['com.apple.security.application-groups']
        assert rights['application-identifier'] == profile['Entitlements']['application-identifier']
        results.append({'identifier': identifier, 'profile': profile['UUID']})
    # Qt's static plugin entry points may have local visibility after linking.
    # Release LTO may inline qInitResources into the retained qrc constructor.
    symbols = output('nm', str(app / 'Society')).decode()
    assert 'society_ios_files_integration_test' not in symbols, 'Disable the Files integration probe before shipping'
    assert 'qml_register_types_LVRS' in symbols, 'Missing LVRS QML registration'
    assert ('qInitResources_qmake_LVRS' in symbols
            or '__GLOBAL__sub_I_qrc_qmake_LVRS.cpp' in symbols), 'Missing LVRS QML resources'
    assert 'qt_static_plugin_QSQLiteDriverPlugin' in symbols, 'Missing static SQLite driver'
    assert 'qt_static_plugin_QDarwinMediaPlugin' in symbols, 'Missing native AVFoundation media backend'
    assert 'qt_static_plugin_QFFmpegMediaPlugin' not in symbols, 'Unpackaged FFmpeg backend must not be imported'
    assert 'restoreSession' in symbols, 'Missing account session restoration API'
    assert 'DNSServiceBrowse' in symbols and 'DNSServiceRegister' in symbols, 'Missing native device discovery'
    # The iOS client strips host-only offer creation during Release linking.
    assert 'verificationCode' in symbols and 'acceptInvitation' in symbols, 'Missing discovery acceptance and code comparison'
    assert ('qInitResources_account_session_license' in symbols
            or '__GLOBAL__sub_I_qrc_account_session_license.cpp' in symbols), 'Missing secure storage license resource'
    assert ('qInitResources_qrcodegen_license' in symbols
            or '__GLOBAL__sub_I_qrc_qrcodegen_license.cpp' in symbols), 'Missing QR generator license resource'
    return {'bundle': str(app), 'appGroup': group, 'signedBundles': results}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--device', required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.bundle.resolve(), args.device), indent=2))

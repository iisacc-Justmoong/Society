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
    symbols = output('nm', str(app / 'Society')).decode()
    assert 'society_ios_files_integration_test' not in symbols, 'Disable the Files integration probe before shipping'
    assert 'qml_register_types_LVRS' in symbols, 'Missing LVRS QML registration'
    assert 'qInitResources_qmake_LVRS' in symbols, 'Missing LVRS QML resources'
    assert 'qt_static_plugin_QSQLiteDriverPlugin' in symbols, 'Missing static SQLite driver'
    return {'bundle': str(app), 'appGroup': group, 'signedBundles': results}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--device', required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.bundle.resolve(), args.device), indent=2))

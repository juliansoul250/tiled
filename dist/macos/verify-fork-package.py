"""Fail-closed audit of the deployed app; record SDK/build identity for support."""
import datetime
import json
import pathlib
import plistlib
import re
import subprocess
import sys

def require(condition, detail):
    if not condition:
        raise RuntimeError(str(detail))
app, kit, source = (pathlib.Path(p).resolve() for p in sys.argv[1:])
expected = '6.11.3'

def run(*args):
    return subprocess.check_output(args, text=True).strip()
require(run(str(kit / 'bin/qmake'), '-query', 'QT_VERSION') == expected, 'Validation requirement failed')

def uuids(binary):
    return sorted(re.findall('UUID: ([0-9A-Fa-f-]+) \\(([^)]+)\\)', run('dwarfdump', '--uuid', str(binary))))

def same_kit(binary, original):
    actual, sdk = (uuids(binary), uuids(original))
    require(actual and set(actual).issubset(set(sdk)), (binary, actual, original, sdk))
    return actual
frameworks = list((app / 'Contents/Frameworks').glob('Qt*.framework'))
require(frameworks, 'No deployed Qt frameworks')
for framework in frameworks:
    info = plistlib.loads((framework / 'Resources/Info.plist').read_bytes())
    # Qt stores major.minor in the display field and the exact patch in BundleVersion.
    require(info['CFBundleVersion'] == expected, (framework, info))
    require(info['CFBundleShortVersionString'] == '.'.join(expected.split('.')[:2]), (framework, info))
    name = framework.stem
    same_kit(framework / name, kit / 'lib' / framework.name / name)
plugin = app / 'Contents/PlugIns/platforms/libqcocoa.dylib'
require(plugin.is_file(), 'Missing Cocoa platform plugin')
require((kit / 'plugins/platforms/libqcocoa.dylib').is_file(), 'Validation requirement failed')
cocoa_uuids = same_kit(plugin, kit / 'plugins/platforms/libqcocoa.dylib')
checked = []
for binary in app.rglob('*'):
    if binary.is_symlink() or not binary.is_file():
        continue
    if 'Mach-O' not in run('file', '-b', str(binary)):
        continue
    dependencies = run('otool', '-L', str(binary)).splitlines()[1:]
    for line in dependencies:
        dependency = line.strip().split(' (')[0]
        require(dependency.startswith(('@rpath/', '@loader_path/', '@executable_path/', '/System/Library/', '/usr/lib/')), (binary, dependency))
        if dependency.startswith('@'):
            prefixes = {'@rpath/': app / 'Contents/Frameworks', '@loader_path/': binary.parent, '@executable_path/': app / 'Contents/MacOS'}
            prefix = next((p for p in prefixes if dependency.startswith(p)))
            resolved = (prefixes[prefix] / dependency[len(prefix):]).resolve()
            require(resolved.is_relative_to(app) and resolved.exists(), (binary, dependency, resolved))
    loads = run('otool', '-l', str(binary)).splitlines()
    for i, line in enumerate(loads):
        if line.strip() == 'cmd LC_RPATH':
            path = loads[i + 2].strip().split('path ', 1)[1].split(' (offset')[0]
            require(path.startswith(('@loader_path', '@executable_path')), (binary, path))
    checked.append(str(binary.relative_to(app)))
identity = {'source_commit': run('git', '-C', str(source), 'rev-parse', 'HEAD'), 'source_dirty': bool(run('git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no')), 'qt_version': expected, 'qt_kit': str(kit), 'cocoa_plugin_uuids': cocoa_uuids, 'qmake': str(kit / 'bin/qmake'), 'macdeployqt': str(kit / 'bin/macdeployqt'), 'built_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'macos': run('sw_vers'), 'architecture': run('uname', '-m'), 'signing': 'ad-hoc; NOT Developer ID signed or notarized', 'qt_frameworks': sorted((p.name for p in frameworks)), 'audited_macho_files': checked}
(app / 'Contents/Resources/fork-build.json').write_text(json.dumps(identity, indent=2) + '\n')
plist_path = app / 'Contents/Info.plist'
plist = plistlib.loads(plist_path.read_bytes())
plist['TiledForkSourceCommit'] = identity['source_commit']
plist['TiledForkQtVersion'] = expected
plist_path.write_bytes(plistlib.dumps(plist))
print(json.dumps(identity, indent=2))

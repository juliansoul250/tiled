"""Install the rebuilt plugin in the cloned kit and record unchanged frameworks."""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys

original, kit, work = map(pathlib.Path, sys.argv[1:4])
archs, target = sys.argv[4:]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
plugins = list((work / 'build').rglob('libqcocoa.dylib'))
if len(plugins) != 1:
    raise RuntimeError(f'Expected exactly one rebuilt Cocoa plugin: {plugins}')
plugin = plugins[0]
actual_archs = set(subprocess.check_output(['lipo', '-archs', str(plugin)], text=True).split())
if actual_archs != set(archs.split(';')):
    raise RuntimeError('Rebuilt plugin architectures differ from selected kit')
actual_targets = set(re.findall(r"^\s*minos\s+(\S+)", subprocess.check_output(['otool', '-l', str(plugin)], text=True), re.MULTILINE))
def version(value):
    return tuple((list(map(int, value.split('.'))) + [0, 0])[:3])
if not actual_targets or any(version(value) != version(target) for value in actual_targets):
    raise RuntimeError(f'Rebuilt plugin deployment targets differ: {actual_targets} vs {target}')
frameworks = {}
for framework in (original / 'lib').glob('Qt*.framework'):
    binary = framework / 'Versions/A' / framework.stem
    relative = binary.relative_to(original)
    if digest(binary) != digest(kit / relative):
        raise RuntimeError(f'Cloned framework changed: {relative}')
    frameworks[str(relative)] = digest(binary)
if not frameworks:
    raise RuntimeError('No Qt framework binaries verified')
destination = kit / 'plugins/platforms/libqcocoa.dylib'
old_hash = digest(destination)
shutil.copy2(plugin, destination)
subprocess.run(['codesign', '--force', '--sign', '-', str(destination)], check=True)
record = {
    'qt_version': '6.11.3',
    'upstream_fix': '8148c48deb8e4345bacb797cb75c011df6f797fa',
    'bug': 'QTBUG-149612',
    'source_sha256': '46de0710b594945a5d446ae44354daa386141c6903c53781b2ca547bc62f8f8f',
    'patch_sha256': 'e0f0ddcb7deff7e01094814880ec3835bd4e63012d16af50bc81e39e7cad92ea',
    'original_plugin_sha256': old_hash, 'patched_plugin_sha256': digest(destination),
    'framework_sha256': frameworks, 'architectures': sorted(actual_archs),
    'deployment_target': target, 'actual_deployment_targets': sorted(actual_targets),
    'compiler': subprocess.check_output(['xcrun', 'clang', '--version'], text=True),
    'plugin_uuids': subprocess.check_output(['dwarfdump', '--uuid', str(destination)], text=True),
}
(kit / 'tiled-cocoa-fix.json').write_text(json.dumps(record, indent=2) + '\n')
shutil.copy2(kit / 'tiled-cocoa-fix.json', work / 'tiled-cocoa-fix.json')
print(json.dumps(record, indent=2))

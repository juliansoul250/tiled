"""Fail-closed audit of the deployed app; record SDK/build identity for support."""
import datetime
import json
import hashlib
import configparser
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
# Catalog bytes must come from the selected SDK, not an unrelated system Qt.
translation_dir = app / 'Contents/Translations'
require((translation_dir / 'tiled_zh_CN.qm').is_file(), '缺少 Tiled 简体中文词典')
qt_translation_source = pathlib.Path(run(str(kit / 'bin/qmake'), '-query', 'QT_INSTALL_TRANSLATIONS'))
qt_catalog = translation_dir / 'qtbase_zh_CN.qm'
require(qt_catalog.is_file(), '缺少 Qt 简体中文词典')
require(qt_catalog.read_bytes() == (qt_translation_source / qt_catalog.name).read_bytes(),
        'Qt 中文词典与所选套件不一致')
conf = configparser.ConfigParser()
conf.read(app / 'Contents/Resources/qt.conf')
require(conf.get('Paths', 'Translations', fallback='') == 'Translations', 'Qt 中文词典搜索路径不正确')
require((app / 'Contents/Resources/tiled-zh_CN-guide.html').is_file(), '缺少离线中文使用说明')
unity = app / 'Contents/Resources/unity'
unity_source = source / 'thirdparty/super-tiled2unity'
require((unity / 'SOURCE.json').is_file(), '缺少 Unity 导入器固定来源记录')
require((unity / 'SOURCE.json').read_bytes() == (unity_source / 'SOURCE.json').read_bytes(),
        'Unity 导入器来源与当前源码不一致')
unity_record = json.loads((unity / 'SOURCE.json').read_text())
require(unity_record['commit'] == 'd3deae415963807caef14c522ed8be9bc30e89e0', 'Unity 导入器提交不符')
unity_package = unity / 'com.seanba.super-tiled2unity'
unity_files = {}
for path in unity_package.rglob('*'):
    require(not path.is_symlink(), 'Unity 导入器禁止符号链接')
    if path.is_file():
        unity_files[path.relative_to(unity_package).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
require(unity_files == unity_record['files'], 'Unity 导入器文件缺失、被修改或混入未知文件')
require((unity / 'UPSTREAM-LICENSE').is_file() and 'LICENSE.md' in unity_files, 'Unity 导入器许可证缺失')
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
audit_errors = []
for binary in app.rglob('*'):
    try:
        if binary.is_symlink() or not binary.is_file():
            continue
        if 'Mach-O' not in run('file', '-b', str(binary)):
            continue
        # Universal files have one header per architecture. Recognize headers
        # explicitly; an unexpected otool diagnostic is not silently discarded.
        dependencies = []
        headers = 0
        for line in run('otool', '-L', str(binary)).splitlines():
            if line.startswith(('\t', ' ')):
                dependencies.append(line)
            elif line.strip():
                require(re.fullmatch(re.escape(str(binary)) + r'( \(architecture [^)]+\))?:', line.strip()),
                        ('Unexpected otool output', binary, line))
                headers += 1
        require(headers and dependencies, ('No loadable Mach-O dependency output', binary))
        loads = run('otool', '-l', str(binary)).splitlines()
        # Audit actual load commands, not otool -L's mixed list of identity and
        # dependencies. This excludes only LC_ID_DYLIB records, even when a real
        # LC_LOAD_* has the same string or another architecture uses a different ID.
        load_types = {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                      'LC_LOAD_UPWARD_DYLIB', 'LC_LAZY_LOAD_DYLIB'}
        loaded_dependencies = []
        for i, line in enumerate(loads):
            if line.strip().startswith('cmd ') and line.strip().split()[1] in load_types:
                name = re.fullmatch(r'name (.+) \(offset \d+\)', loads[i + 2].strip())
                require(name, ('Malformed dylib load command', binary, loads[i:i + 3]))
                loaded_dependencies.append(name.group(1))
        require(loaded_dependencies, ('No dynamic library load commands', binary))
        for dependency in loaded_dependencies:
            require(dependency.startswith(('@rpath/', '@loader_path/', '@executable_path/', '/System/Library/', '/usr/lib/')), (binary, dependency))
            if dependency.startswith('@'):
                prefixes = {'@rpath/': app / 'Contents/Frameworks', '@loader_path/': binary.parent, '@executable_path/': app / 'Contents/MacOS'}
                prefix = next((p for p in prefixes if dependency.startswith(p)))
                resolved = (prefixes[prefix] / dependency[len(prefix):]).resolve()
                require(resolved.is_relative_to(app) and resolved.exists(), (binary, dependency, resolved))
        for i, line in enumerate(loads):
            if line.strip() == 'cmd LC_RPATH':
                path = loads[i + 2].strip().split('path ', 1)[1].split(' (offset')[0]
                prefix = '@loader_path' if path.startswith('@loader_path') else '@executable_path'
                require(path == prefix or path.startswith(prefix + '/'), (binary, path))
                anchor = binary.parent if prefix == '@loader_path' else app / 'Contents/MacOS'
                resolved = (anchor / path[len(prefix):].lstrip('/')).resolve()
                # A bundle-contained search hint may be unused/nonexistent after deploy.
                # Actual LC_LOAD targets above still must resolve to existing files.
                require(resolved.is_relative_to(app), (binary, path, resolved))
        checked.append(str(binary.relative_to(app)))
    except Exception as error:
        audit_errors.append(f"{binary}: {type(error).__name__}: {error}")
require(not audit_errors, 'Deployed library audit failed:\n' + '\n'.join(audit_errors))
identity = {'source_commit': run('git', '-C', str(source), 'rev-parse', 'HEAD'), 'source_dirty': bool(run('git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no')), 'qt_version': expected, 'ui_language': 'zh_CN', 'unity_importer_commit': unity_record['commit'], 'qt_chinese_catalog_sha256': hashlib.sha256(qt_catalog.read_bytes()).hexdigest(), 'qt_kit': str(kit), 'cocoa_plugin_uuids': cocoa_uuids, 'qmake': str(kit / 'bin/qmake'), 'macdeployqt': str(kit / 'bin/macdeployqt'), 'built_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'macos': run('sw_vers'), 'architecture': run('uname', '-m'), 'signing': 'ad-hoc; NOT Developer ID signed or notarized', 'qt_frameworks': sorted((p.name for p in frameworks)), 'audited_macho_files': checked}
(app / 'Contents/Resources/fork-build.json').write_text(json.dumps(identity, indent=2) + '\n')
plist_path = app / 'Contents/Info.plist'
plist = plistlib.loads(plist_path.read_bytes())
plist['TiledForkSourceCommit'] = identity['source_commit']
plist['TiledForkQtVersion'] = expected
plist_path.write_bytes(plistlib.dumps(plist))
print(json.dumps(identity, indent=2))

"""CI-only fail-closed Windows portable-package audit (requires pefile).

This static import closure complements, but does not replace, a relocated launch
and inspection of the process's actual loaded modules. No SDK is needed by users.
"""
import argparse
import configparser
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

QT_VERSION = '6.11.3'
QT_LICENSE_COMMITS = {
    'qtbase': '138f955f0b5e50e4a6c2ee1e78a2279db52ae9e3',
    'qtdeclarative': '95e4ceccb77c82411f8a048eda4360e5967f4e60',
    'qtsvg': 'f5d99721911e4a1989965a087813cf1277519bc6',
    'qtimageformats': '434c41f81a825e0abcda13ff93ef30bc0e155d1d',
}
UNITY_COMMIT = 'd3deae415963807caef14c522ed8be9bc30e89e0'
# Windows OS components only; notably excludes VC redistributables and Qt.
SYSTEM_DLLS = set('''advapi32 avrt bcrypt bcryptprimitives cabinet cfgmgr32 comctl32
comdlg32 credui crypt32 cryptbase cryptnet cryptsp d2d1 d3d9 d3d11 d3d12 dbghelp
 dcomp dnsapi dwmapi dwrite dxgi dxguid dxva2 gdi32 gdi32full hid imm32 iphlpapi
 kernel32 kernelbase msvcrt icuuc icuin icu mpr msimg32 mswsock ncrypt netapi32 normaliz ntdll ole32
 oleacc oleaut32 opengl32 powrprof propsys psapi rasapi32 rpcrt4 secur32 setupapi
 shell32 shcore shlwapi user32 userenv uiautomationcore usp10 uxtheme version winhttp wininet
 winmm winspool wintrust wldap32 ws2_32 wtsapi32 ucrtbase msasn1 mf mfplat
 mfreadwrite mfuuid strmiids windowscodecs win32u cryptui dhcpcsvc dhcpcsvc6
 authz nsi profapi rstrtmgr usermgr wkscli winnsi winsta'''.split())
VC_DLL = re.compile(r'^(?:vcruntime\d+|msvcp\d+|concrt\d+|vcomp\d+)(?:_[a-z0-9]+)*\.dll$', re.I)
DEBUG_DLL = re.compile(r'^(?:qt6.+d|vcruntime\d+(?:_\d+)?d|msvcp\d+(?:_\d+)?d|ucrtbased)\.dll$', re.I)


def require(condition, message):
    if not condition:
        raise RuntimeError(str(message))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def pe_imports(path):
    import pefile  # CI-only; keep portable unit tests independent of this package.
    with pefile.PE(str(path), fast_load=True) as pe:
        require(pe.FILE_HEADER.Machine == 0x8664, f'非 AMD64 二进制：{path}')
        require(pe.OPTIONAL_HEADER.Magic == 0x20b, f'非 PE32+ 二进制：{path}')
        pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']])
        result = []
        for attribute in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT'):
            for entry in getattr(pe, attribute, []):
                result.append(entry.dll.decode('ascii').lower())
        require(result, f'二进制没有可审核的导入表：{path}')
        return result


def safe_files(root):
    result = {}
    for path in root.rglob('*'):
        require(not path.is_symlink() and not getattr(path, 'is_junction', lambda: False)(),
                f'包内不允许符号链接或目录联接：{path}')
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        key = relative.lower()
        require(key not in result, f'Windows 文件名大小写冲突：{relative}')
        require(path.resolve().is_relative_to(root.resolve()), f'文件逃逸包目录：{path}')
        result[key] = path
    return result


def audit_imports(root, kit, vc_redist, *, inspect=pe_imports, system32=None):
    files = safe_files(root)
    system32 = system32 or Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32'
    kit_files = {}
    for folder in ('bin', 'plugins'):
        for file in (kit / folder).rglob('*.dll'):
            if file.is_file():
                kit_files.setdefault(file.name.lower(), []).append(file)
    vc_files = {p.name.lower(): p for p in vc_redist.glob('*.dll') if p.is_file()}
    checked = {}
    for relative, binary in files.items():
        if binary.suffix.lower() not in ('.dll', '.exe'):
            continue
        name = binary.name.lower()
        require(not DEBUG_DLL.fullmatch(name), f'发布包混入调试运行库：{relative}')
        require(name != 'qt6test.dll', f'发布包混入诊断运行库：{relative}')
        if (not VC_DLL.fullmatch(name) and name in kit_files) or name.startswith('qt6') or relative.startswith(('platforms/', 'imageformats/', 'iconengines/', 'styles/', 'tls/', 'networkinformation/')):
            candidates = kit_files.get(name, [])
            require(not (name.endswith('d.dll') and name[:-5] + '.dll' in kit_files),
                    f'发布包混入 Qt 调试插件：{relative}')
            require(candidates and any(sha(binary) == sha(original) for original in candidates),
                    f'Qt DLL/插件与固定套件不一致：{relative}')
        if VC_DLL.fullmatch(name):
            require('/' not in relative and name in vc_files and sha(binary) == sha(vc_files[name]),
                    f'VC 运行库必须来自指定官方 redist 并放于应用根目录：{relative}')
        imports = inspect(binary)
        for dependency in imports:
            dependency = dependency.lower()
            require(re.fullmatch(r'[a-z0-9_.-]+\.dll', dependency), f'非法 DLL 名称：{dependency}')
            require(not DEBUG_DLL.fullmatch(dependency), f'导入了调试 DLL：{relative}: {dependency}')
            local = files.get(dependency)
            sibling_key = (binary.parent.relative_to(root) / dependency).as_posix().lower()
            sibling = files.get(sibling_key)
            if VC_DLL.fullmatch(dependency):
                require(local is not None and dependency in vc_files,
                        f'缺少应用本地 VC 运行库：{relative}: {dependency}')
            elif local or sibling:
                pass  # Every deployed PE is separately audited, including its imports.
            elif dependency.startswith(('api-ms-win-', 'ext-ms-win-')):
                pass  # OS API-set contracts, resolved by Windows rather than a disk DLL.
            else:
                require(dependency[:-4] in SYSTEM_DLLS and (system32 / dependency).is_file(),
                        f'缺少依赖或依赖不属于允许的系统 DLL：{relative}: {dependency}')
        checked[relative] = {'sha256': sha(binary), 'imports': sorted(set(imports))}
    require(checked, '没有可审核的 PE 文件')
    return checked


def audit_data(root, kit, source):
    files = safe_files(root)
    for required in ('tiled.exe', 'qt6core.dll', 'qt6gui.dll', 'qt6widgets.dll',
                     'platforms/qwindows.dll', 'translations/tiled_zh_cn.qm',
                     'translations/qtbase_zh_cn.qm', 'translations/qt_zh_cn.qm',
                     'doc/tiled-zh_cn-guide.html', 'doc/unity-import-zh_cn.md',
                     'authors', 'copying', 'license.gpl', 'license.apache', 'license.bsd'):
        require(required in files and files[required].stat().st_size > 0, f'缺少文件：{required}')
    for name, path in files.items():
        require(path.suffix.lower() not in ('.pdb', '.lib', '.exp', '.obj', '.ilk', '.prl', '.a'),
                f'发布包混入开发文件：{name}')
        require(not path.name.lower().startswith(('test_', 'qmake', 'windeployqt')), f'发布包混入诊断/开发工具：{name}')
    conf = configparser.ConfigParser()
    conf.read(root / 'qt.conf', encoding='utf-8-sig')
    for key, expected in (('Prefix', '.'), ('Plugins', '.'), ('Translations', 'translations')):
        require(conf.get('Paths', key, fallback='') == expected, f'qt.conf {key} 不正确')
    for name in ('qtbase_zh_CN.qm', 'qt_zh_CN.qm'):
        require(sha(root / 'translations' / name) == sha(kit / 'translations' / name), f'Qt 中文词典不是所选套件：{name}')
    license_root = source / 'thirdparty/qt-licenses-6.11.3'
    license_record = json.loads((license_root / 'SOURCE.json').read_text(encoding='utf-8'))
    require(license_record.get('version') == QT_VERSION and
            license_record.get('commits') == QT_LICENSE_COMMITS, 'Qt 许可证来源不是固定的同版官方提交')
    qt_license_source = safe_files(license_root)
    license_hashes = {p.relative_to(license_root).as_posix(): sha(p)
                      for p in qt_license_source.values() if p != license_root / 'SOURCE.json'}
    require(license_hashes and license_hashes == license_record.get('files'), 'Qt 许可证来源文件校验失败')
    for module in QT_LICENSE_COMMITS:
        require(any(n.startswith(module + '/LICENSES/') for n in license_hashes),
                f'Qt 许可证缺少组件：{module}')
    qt_license_deployed = safe_files(root / 'licenses/Qt')
    require({n: sha(p) for n, p in qt_license_source.items()} ==
            {n: sha(p) for n, p in qt_license_deployed.items()}, 'Qt 许可证缺失或与固定官方来源不一致')
    vendor = root / 'unity'
    record = json.loads((vendor / 'SOURCE.json').read_text(encoding='utf-8'))
    original = json.loads((source / 'thirdparty/super-tiled2unity/SOURCE.json').read_text(encoding='utf-8'))
    require(record == original and record['commit'] == UNITY_COMMIT and record['version'] == '2.4.0', 'Unity 固定来源不一致')
    package = vendor / 'com.seanba.super-tiled2unity'
    actual = {p.relative_to(package).as_posix(): sha(p) for p in safe_files(package).values()}
    require(len(actual) == 582 and actual == record['files'], 'Unity 导入器文件缺失、改变或混入未知文件')
    require((vendor / 'UPSTREAM-LICENSE').read_bytes() == (source / 'thirdparty/super-tiled2unity/UPSTREAM-LICENSE').read_bytes(), 'Unity 上游许可证不一致')
    require('LICENSE.md' in actual, 'Unity 包许可证缺失')
    return record



def record_identity(root, identity, verify_only=False):
    path = root / 'fork-build.json'
    if verify_only:
        require(path.is_file(), '缺少归档内的构建身份记录')
        existing = json.loads(path.read_text(encoding='utf-8'))
        # The original timestamp describes the build, not this later verification.
        require(isinstance(existing.get('built_utc'), str) and existing['built_utc'], '缺少原始构建时间')
        expected = dict(identity)
        expected['built_utc'] = existing['built_utc']
        require(existing == expected, '归档构建身份或 PE 文件摘要与当前审核不一致')
        return existing
    path.write_text(json.dumps(identity, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('kit', type=Path)
    parser.add_argument('source', type=Path)
    parser.add_argument('--vc-redist', type=Path, default=os.environ.get('TILED_VC_REDIST_DIR'))
    parser.add_argument('--verify-only', action='store_true', help='验证解压后的归档，不改写任何文件')
    args = parser.parse_args()
    root, kit, source = (p.resolve() for p in (args.root, args.kit, args.source))
    require(args.vc_redist and args.vc_redist.is_dir(), '必须指定官方 VC redist 目录')
    require(run(str(kit / 'bin/qmake.exe'), '-query', 'QT_VERSION') == QT_VERSION, '必须使用 Qt 6.11.3')
    record = audit_data(root, kit, source)
    binaries = audit_imports(root, kit, args.vc_redist.resolve())
    dirty = bool(run('git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'))
    require(not dirty, '打包源码存在未提交修改')
    identity = {'source_commit': run('git', '-C', str(source), 'rev-parse', 'HEAD'),
                'source_dirty': dirty, 'qt_version': QT_VERSION, 'architecture': 'x86_64',
                'ui_language': 'zh_CN', 'signing': 'unsigned; no Authenticode certificate',
                'unity_importer_commit': record['commit'], 'qt_kit': str(kit),
                'vc_redist': str(args.vc_redist.resolve()), 'audited_pe_files': binaries,
                'built_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    identity = record_identity(root, identity, args.verify_only)
    print(json.dumps(identity, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

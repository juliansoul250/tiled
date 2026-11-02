"""Cross-platform negative contracts for the Windows delivery audit."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'dist/windows/verify_package.py'
spec = importlib.util.spec_from_file_location('windows_audit', SCRIPT)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class WindowsAudit(unittest.TestCase):
    def test_windows_builtin_icu_requires_system_file(self):
        self.dependencies['tiled.exe'] = ['icuuc.dll']
        with self.assertRaises(RuntimeError):
            self.run_audit()
        self.put(self.system, 'icuuc.dll')
        self.run_audit()
        self.dependencies['tiled.exe'] = ['icuuc99.dll']
        self.put(self.system, 'icuuc99.dll')
        with self.assertRaises(RuntimeError):
            self.run_audit()

    def test_loaded_runtime_classification(self):
        smoke_path = Path(__file__).with_name('windows_app_smoke.py')
        smoke_spec = importlib.util.spec_from_file_location('windows_smoke_contract', smoke_path)
        smoke = importlib.util.module_from_spec(smoke_spec)
        # This contract only tests name classification, not third-party GUI APIs.
        with patch.dict('sys.modules', {'psutil': SimpleNamespace(), 'PIL': SimpleNamespace(ImageGrab=None)}):
            smoke_spec.loader.exec_module(smoke)
        self.assertFalse(smoke.requires_app_local('msvcp_win.dll'))
        for name in ('msvcp140.dll', 'MSVCP140_1.DLL', 'vcruntime140_1.dll',
                     'concrt140.dll', 'vcomp140.dll', 'Qt6Core.dll', 'qwindows.dll'):
            self.assertTrue(smoke.requires_app_local(name), name)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root, self.kit, self.vc, self.system, self.source = (self.base / n for n in ('app', 'kit', 'vc', 'system', 'source'))
        for p in (self.root, self.kit, self.vc, self.system, self.source):
            p.mkdir()
        self.put(self.root, 'tiled.exe')
        self.put(self.system, 'kernel32.dll')
        self.dependencies = {'tiled.exe': ['kernel32.dll']}

    def put(self, root, relative, content=b'fixture'):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def run_audit(self):
        return audit.audit_imports(self.root, self.kit, self.vc,
            inspect=lambda p: self.dependencies.get(p.name.lower(), ['kernel32.dll']), system32=self.system)

    def test_pe_parser_reads_delay_imports_and_checks_amd64(self):
        class FakePE:
            FILE_HEADER = SimpleNamespace(Machine=0x8664)
            OPTIONAL_HEADER = SimpleNamespace(Magic=0x20b)
            DIRECTORY_ENTRY_IMPORT = [SimpleNamespace(dll=b'KERNEL32.dll')]
            DIRECTORY_ENTRY_DELAY_IMPORT = [SimpleNamespace(dll=b'missing-lazy.dll')]
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def parse_data_directories(self, **kwargs):
                self.directories = kwargs['directories']
        fake = FakePE()
        module = SimpleNamespace(PE=lambda *a, **k: fake,
            DIRECTORY_ENTRY={'IMAGE_DIRECTORY_ENTRY_IMPORT': 1, 'IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT': 13})
        with patch.dict('sys.modules', {'pefile': module}):
            self.assertEqual(audit.pe_imports(self.root / 'tiled.exe'), ['kernel32.dll', 'missing-lazy.dll'])
            self.assertEqual(fake.directories, [1, 13])
            fake.FILE_HEADER = SimpleNamespace(Machine=0x14c)
            with self.assertRaisesRegex(RuntimeError, 'AMD64'):
                audit.pe_imports(self.root / 'tiled.exe')

    def test_verify_only_preserves_manifest_bytes(self):
        identity = {'source_commit': 'abcd', 'qt_version': audit.QT_VERSION,
                    'audited_pe_files': {'tiled.exe': {'sha256': 'fixture'}}, 'built_utc': 'original'}
        audit.record_identity(self.root, identity)
        before = (self.root / 'fork-build.json').read_bytes()
        later = dict(identity, built_utc='later')
        audit.record_identity(self.root, later, True)
        self.assertEqual((self.root / 'fork-build.json').read_bytes(), before)
        later['source_commit'] = 'other'
        with self.assertRaisesRegex(RuntimeError, '构建身份'):
            audit.record_identity(self.root, later, True)
        self.assertEqual((self.root / 'fork-build.json').read_bytes(), before)

    def test_verify_only_missing_manifest_rejected(self):
        with self.assertRaisesRegex(RuntimeError, '缺少归档'):
            audit.record_identity(self.root, {'built_utc': 'later'}, True)

    def test_system_and_api_set(self):
        self.dependencies['tiled.exe'].append('api-ms-win-core-file-l1-1-0.dll')
        self.assertIn('tiled.exe', self.run_audit())

    def test_missing_unknown_dll_even_if_present_in_system(self):
        self.dependencies['tiled.exe'].append('unexpected.dll')
        self.put(self.system, 'unexpected.dll')
        with self.assertRaisesRegex(RuntimeError, '缺少依赖'):
            self.run_audit()

    def test_known_system_missing(self):
        (self.system / 'kernel32.dll').unlink()
        with self.assertRaises(RuntimeError):
            self.run_audit()

    def test_vc_must_be_app_local_not_system(self):
        self.dependencies['tiled.exe'].append('vcruntime140.dll')
        self.put(self.vc, 'vcruntime140.dll')
        self.put(self.system, 'vcruntime140.dll')
        with self.assertRaisesRegex(RuntimeError, '本地 VC'):
            self.run_audit()
        self.put(self.root, 'vcruntime140.dll')
        self.run_audit()

    def test_vc_mismatched_source(self):
        self.put(self.root, 'msvcp140.dll', b'bad')
        self.put(self.vc, 'msvcp140.dll')
        with self.assertRaisesRegex(RuntimeError, 'redist'):
            self.run_audit()

    def test_matching_qt_and_plugin(self):
        self.put(self.root, 'Qt6Core.dll')
        self.put(self.kit, 'bin/Qt6Core.dll')
        self.put(self.root, 'platforms/qwindows.dll')
        self.put(self.kit, 'plugins/platforms/qwindows.dll')
        self.dependencies['qwindows.dll'] = ['Qt6Core.dll']
        self.run_audit()
        (self.root / 'platforms/qwindows.dll').write_bytes(b'other kit')
        with self.assertRaisesRegex(RuntimeError, '固定套件'):
            self.run_audit()

    def test_release_qttest_diagnostic_rejected(self):
        self.put(self.root, 'Qt6Test.dll')
        self.put(self.kit, 'bin/Qt6Test.dll')
        with self.assertRaisesRegex(RuntimeError, '诊断运行库'):
            self.run_audit()

    def test_debug_qt_plugin_rejected(self):
        self.put(self.root, 'platforms/qwindowsd.dll')
        self.put(self.kit, 'plugins/platforms/qwindowsd.dll')
        self.put(self.kit, 'plugins/platforms/qwindows.dll')
        with self.assertRaisesRegex(RuntimeError, '调试插件'):
            self.run_audit()

    def test_recursive_import_missing(self):
        self.put(self.root, 'editor.dll')
        self.dependencies['tiled.exe'] = ['editor.dll']
        self.dependencies['editor.dll'] = ['missing.dll']
        with self.assertRaisesRegex(RuntimeError, 'missing.dll'):
            self.run_audit()

    def test_debug_import_rejected(self):
        self.dependencies['tiled.exe'].append('Qt6Cored.dll')
        with self.assertRaisesRegex(RuntimeError, '调试 DLL'):
            self.run_audit()

    def test_path_import_rejected(self):
        self.dependencies['tiled.exe'].append('../kernel32.dll')
        with self.assertRaisesRegex(RuntimeError, '非法 DLL'):
            self.run_audit()

    def test_symlink_rejected(self):
        try:
            (self.root / 'link.dll').symlink_to(self.system / 'kernel32.dll')
        except OSError:
            self.skipTest('Account cannot create symlinks')
        with self.assertRaisesRegex(RuntimeError, '符号链接'):
            self.run_audit()

    def data_fixture(self):
        for relative in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll',
                         'translations/tiled_zh_CN.qm', 'translations/qtbase_zh_CN.qm',
                         'translations/qt_zh_CN.qm', 'doc/tiled-zh_CN-guide.html',
                         'doc/unity-import-zh_CN.md', 'AUTHORS', 'COPYING',
                         'LICENSE.GPL', 'LICENSE.APACHE', 'LICENSE.BSD'):
            self.put(self.root, relative)
        license_files = {}
        for module in audit.QT_LICENSE_COMMITS:
            name = module + '/LICENSES/LGPL-3.0-only.txt'
            original = self.put(self.source, 'thirdparty/qt-licenses-6.11.3/' + name)
            self.put(self.root, 'licenses/Qt/' + name)
            license_files[name] = audit.sha(original)
        record = {'version': audit.QT_VERSION, 'commits': audit.QT_LICENSE_COMMITS, 'files': license_files}
        encoded = json.dumps(record).encode()
        self.put(self.source, 'thirdparty/qt-licenses-6.11.3/SOURCE.json', encoded)
        self.put(self.root, 'licenses/Qt/SOURCE.json', encoded)
        self.put(self.kit, 'translations/qtbase_zh_CN.qm')
        self.put(self.kit, 'translations/qt_zh_CN.qm')
        self.put(self.root, 'qt.conf', b'[Paths]\nPrefix=.\nPlugins=.\nTranslations=translations\n')
        package = self.root / 'unity/com.seanba.super-tiled2unity'
        for i in range(581):
            self.put(package, f'Editor/{i}.cs')
        self.put(package, 'LICENSE.md')
        hashes = {p.relative_to(package).as_posix(): audit.sha(p) for p in package.rglob('*') if p.is_file()}
        record = {'commit': audit.UNITY_COMMIT, 'version': '2.4.0', 'files': hashes}
        encoded = json.dumps(record).encode()
        self.put(self.root, 'unity/SOURCE.json', encoded)
        self.put(self.source, 'thirdparty/super-tiled2unity/SOURCE.json', encoded)
        self.put(self.root, 'unity/UPSTREAM-LICENSE')
        self.put(self.source, 'thirdparty/super-tiled2unity/UPSTREAM-LICENSE')

    def test_data_complete(self):
        self.data_fixture()
        audit.audit_data(self.root, self.kit, self.source)

    def test_vendor_extra_file_rejected(self):
        self.data_fixture()
        self.put(self.root, 'unity/com.seanba.super-tiled2unity/unknown.cs')
        with self.assertRaisesRegex(RuntimeError, '导入器文件'):
            audit.audit_data(self.root, self.kit, self.source)

    def test_development_file_rejected(self):
        self.data_fixture()
        self.put(self.root, 'tiled.pdb')
        with self.assertRaisesRegex(RuntimeError, '开发文件'):
            audit.audit_data(self.root, self.kit, self.source)

    def test_missing_qt_license_rejected(self):
        self.data_fixture()
        (self.root / 'licenses/Qt/qtbase/LICENSES/LGPL-3.0-only.txt').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Qt 许可证'):
            audit.audit_data(self.root, self.kit, self.source)

    def test_wrong_qt_license_source_rejected(self):
        self.data_fixture()
        self.put(self.source, 'thirdparty/qt-licenses-6.11.3/qtbase/LICENSES/LGPL-3.0-only.txt', b'changed')
        with self.assertRaisesRegex(RuntimeError, 'Qt 许可证'):
            audit.audit_data(self.root, self.kit, self.source)

    def test_unknown_qt_license_file_rejected(self):
        self.data_fixture()
        self.put(self.root, 'licenses/Qt/untracked.txt')
        with self.assertRaisesRegex(RuntimeError, 'Qt 许可证'):
            audit.audit_data(self.root, self.kit, self.source)

    def test_wrong_qt_catalog_rejected(self):
        self.data_fixture()
        self.put(self.root, 'translations/qtbase_zh_CN.qm', b'other')
        with self.assertRaisesRegex(RuntimeError, '中文词典'):
            audit.audit_data(self.root, self.kit, self.source)

    def test_wrong_translation_path_rejected(self):
        self.data_fixture()
        self.put(self.root, 'qt.conf', b'[Paths]\nPrefix=.\nPlugins=.\nTranslations=/sdk/translations\n')
        with self.assertRaisesRegex(RuntimeError, 'qt.conf'):
            audit.audit_data(self.root, self.kit, self.source)


if __name__ == '__main__':
    unittest.main()

"""Portable contract tests for package audit parsing and fail-closed decisions."""
import pathlib
import plistlib
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'dist/macos/verify-fork-package.py'

class PackageAudit(unittest.TestCase):
    def audit(self, *, version='6.11.3', dependency=None, uuid_mismatch=False, bad_header=False, empty_output=False, install_id=None, rpath='@executable_path/../Frameworks'):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            app, kit, source = (root / x for x in ('Tiled.app', 'qt', 'source'))
            framework = app / 'Contents/Frameworks/QtCore.framework'
            for p in (framework / 'Resources', app / 'Contents/PlugIns/platforms',
                      app / 'Contents/MacOS', app / 'Contents/Resources',
                      kit / 'plugins/platforms', source):
                p.mkdir(parents=True)
            (framework / 'Resources/Info.plist').write_bytes(plistlib.dumps({
                'CFBundleShortVersionString': '6.11', 'CFBundleVersion': version}))
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({}))
            for p in (framework / 'QtCore', app / 'Contents/MacOS/Tiled',
                      app / 'Contents/PlugIns/platforms/libqcocoa.dylib',
                      kit / 'plugins/platforms/libqcocoa.dylib'):
                p.write_bytes(b'MACHO')
            def command(args, **kwargs):
                tool = pathlib.Path(args[0]).name
                if tool == 'qmake': return '6.11.3\n'
                if tool == 'file':
                    return 'Mach-O universal binary' if pathlib.Path(args[-1]).read_bytes() == b'MACHO' else 'data'
                if tool == 'dwarfdump':
                    value = 'BBBB' if uuid_mismatch and str(kit) in args[-1] else 'AAAA'
                    return f'UUID: {value} (arm64) {args[-1]}\n'
                if tool == 'otool':
                    if args[1] == '-l':
                        identity = f'cmd LC_ID_DYLIB\ncmdsize 48\nname {install_id} (offset 24)\n' if install_id else ''
                        dep = dependency or '@rpath/QtCore.framework/QtCore'
                        load = f'cmd LC_LOAD_DYLIB\ncmdsize 48\nname {dep} (offset 24)\n'
                        return identity + load + f'cmd LC_RPATH\ncmdsize 48\npath {rpath} (offset 12)\n'
                    if empty_output: return ''
                    if bad_header: return 'ERROR: invalid library output\n'
                    dep = dependency or '@rpath/QtCore.framework/QtCore'
                    return f'{args[-1]} (architecture arm64):\n\t{dep} (compatibility version 6.0.0)\n{args[-1]} (architecture x86_64):\n\t{dep} (compatibility version 6.0.0)\n'
                if tool == 'git': return '' if 'status' in args else 'a' * 40
                if tool == 'uname': return 'arm64'
                if tool == 'sw_vers': return 'macOS test fixture'
                raise AssertionError(args)
            with patch.object(sys, 'argv', [str(SCRIPT), str(app), str(kit), str(source)]), \
                    patch.object(subprocess, 'check_output', side_effect=command), patch('builtins.print'):
                runpy.run_path(str(SCRIPT), run_name='__main__')
            self.assertTrue((app / 'Contents/Resources/fork-build.json').is_file())

    def test_universal_headers_and_qt_version_fields(self): self.audit()
    def test_unexpected_otool_output_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(bad_header=True)
    def test_empty_otool_output_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(empty_output=True)
    def test_bare_dylib_identity_is_not_external_dependency(self):
        self.audit(install_id='libexample.dylib')
    def test_real_load_matching_self_identity_still_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(dependency='libexample.dylib', install_id='libexample.dylib')
    def test_bare_loaded_dependency_still_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(dependency='liboutside.dylib', install_id='libexample.dylib')
    def test_wrong_patch_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(version='6.11.2')
    def test_wrong_kit_uuid_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(uuid_mismatch=True)
    def test_external_library_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(dependency='/opt/homebrew/lib/libwrong.dylib')
    def test_missing_relative_library_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(dependency='@rpath/missing.dylib')
    def test_escaping_relative_library_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(dependency='@executable_path/../../../../outside.dylib')
    def test_escaping_rpath_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(rpath='@loader_path/../../../../../../outside')
    def test_external_rpath_rejected(self):
        with self.assertRaises(RuntimeError): self.audit(rpath='/Users/builder/Qt/lib')

if __name__ == '__main__': unittest.main()

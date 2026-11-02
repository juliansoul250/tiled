#!/usr/bin/env python3
"""CI-only native Windows validation. No Python is required by the shipped app."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

import psutil
from PIL import ImageGrab


def isolated_environment(results):
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith(('QT_', 'QML', 'DYLD_', 'TILED_', 'PYTHON'))}
    windows = Path(os.environ['SystemRoot'])
    env['PATH'] = str(windows / 'System32') + os.pathsep + str(windows)
    env.update(QT_FORCE_STDERR_LOGGING='1', QT_QPA_PLATFORM='windows',
               TILED_WIDGET_ARTIFACTS=str(results))
    return env


# msvcp_win.dll is a Windows component, not the versioned VC redistributable.
VC_DLL = re.compile(r"^(?:vcruntime\d+|msvcp\d+|concrt\d+|vcomp\d+)(?:_[a-z0-9]+)*\.dll$", re.I)

def requires_app_local(name):
    name = name.lower()
    return name.startswith("qt6") or name == "qwindows.dll" or bool(VC_DLL.fullmatch(name))

def modules(pid, app):
    app = app.resolve()
    windows = Path(os.environ['SystemRoot']).resolve()
    paths = sorted({str(Path(m.path).resolve()) for m in psutil.Process(pid).memory_maps()
                    if m.path.lower().endswith(('.dll', '.exe'))})
    for text in paths:
        path = Path(text)
        local = path.is_relative_to(app)
        if not local and not path.is_relative_to(windows):
            raise RuntimeError('Loaded module outside application / Windows: ' + text)
        name = path.name.lower()
        if requires_app_local(name):
            if not local:
                raise RuntimeError('Qt / VC runtime was not loaded from portable app: ' + text)
    for required in ('qt6core.dll', 'qt6widgets.dll', 'qwindows.dll'):
        if required not in {Path(p).name.lower() for p in paths}:
            raise RuntimeError('Required native module not loaded: ' + required)
    return paths


def window_for(pid):
    user = ctypes.windll.user32
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    found = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd):
            title = ctypes.create_unicode_buffer(1024)
            user.GetWindowTextW(hwnd, title, len(title))
            if 'desert.tmx' in title.value:
                found.append((hwnd, title.value))
        return True
    user.EnumWindows(visit, 0)
    return found[0] if found else None


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--app', type=Path)
    p.add_argument('--diagnostic', type=Path)
    p.add_argument('--results', type=Path, required=True)
    p.add_argument('--sample', type=Path)
    p.add_argument('--seconds', type=int, default=900)
    a = p.parse_args()
    a.results = a.results.resolve()
    a.results.mkdir(parents=True, exist_ok=True)
    if os.name != 'nt':
        raise RuntimeError('Requires real Windows qwindows session')
    env = isolated_environment(a.results)
    if a.diagnostic:
        app = a.diagnostic.resolve()
        with (a.results / 'unity-ui-console.txt').open('w', encoding='utf-8') as log:
            subprocess.run([str(app / 'test_unityui.exe'), '-o', str(a.results / 'unity-ui.txt') + ',txt'],
                           cwd=app, env=env, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=180)
        return
    app = a.app.resolve()
    if a.seconds < 900:
        raise RuntimeError('Production hold must be at least 900 seconds')
    # Copy the known sample to a new Unicode/space path, not the build/source tree.
    with tempfile.TemporaryDirectory(prefix='Tiled 示例地图 ') as directory:
        sample = Path(directory)
        for name in ('desert.tmx', 'desert.tsx', 'tmw_desert_spacing.png'):
            shutil.copy2(a.sample.parent / name, sample / name)
        log_path = a.results / 'production-app-console.txt'
        with log_path.open('w', encoding='utf-8') as log:
            process = subprocess.Popen([str(app / 'tiled.exe'), str(sample / 'desert.tmx')],
                                       cwd=sample, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 60
                window = None
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError('Application exited before map exposure: ' + str(process.returncode))
                    window = window_for(process.pid)
                    if window:
                        break
                    time.sleep(.5)
                if not window:
                    raise RuntimeError('No visible real Windows map window appeared')
                time.sleep(3)
                captured = ImageGrab.grab(all_screens=True)
                captured.save(a.results / 'production-app-desert.png')
                loaded = modules(process.pid, app)
                started = time.monotonic()
                while time.monotonic() - started < a.seconds:
                    if process.poll() is not None or not window_for(process.pid):
                        raise RuntimeError('Application/window terminated during 15-minute hold')
                    time.sleep(1)
                loaded = sorted(set(loaded + modules(process.pid, app)))
                (a.results / 'loaded-modules.json').write_text(json.dumps(loaded, indent=2), encoding='utf-8')
                ctypes.windll.user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
                ctypes.windll.user32.PostMessageW(window[0], 0x0010, 0, 0)  # normal WM_CLOSE, never kill to pass
                code = process.wait(timeout=30)
                if code != 0:
                    raise RuntimeError('Normal close returned ' + str(code))
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        console = log_path.read_text(encoding='utf-8', errors='replace')
        for expected in ('language=zh_CN app=1 qt=1', 'Unity transfer menu: 导入到 Unity'):
            if expected not in console:
                raise RuntimeError('Missing actual Chinese runtime evidence: ' + expected)
        (a.results / 'production-result.json').write_text(json.dumps({
            'hold_seconds': a.seconds, 'exit_code': code, 'window_title': window[1],
            'platform': 'windows', 'app': str(app), 'child_path': env['PATH'],
            'unity_editor_executed': False,
            'source_commit': json.loads((app / 'fork-build.json').read_text())['source_commit'],
        }, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

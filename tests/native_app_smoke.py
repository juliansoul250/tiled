"""Bounded smoke of the actual built application on a disposable macOS runner.

No test hooks in production, input automation privileges, or security bypasses.
The runner owns the only launched process; failure cleanup targets its group.
"""
import json
import os
import pathlib
import signal
import subprocess
import time

out = pathlib.Path(os.environ['TILED_WIDGET_ARTIFACTS'])
apps = [p.resolve() for p in pathlib.Path('debug/install-root').rglob('Tiled.app')
        if (p / 'Contents/MacOS/Tiled').is_file()]
if len(apps) != 1:
    raise RuntimeError(f'Expected one built application, found {apps}')
app = apps[0]
# This is an isolated GitHub runner account, not a user's preferences/session.
env = os.environ.copy()
env.pop('TILED_EXPECT_SYSTEM_REDUCED_TRANSPARENCY', None)
env['QT_LOGGING_RULES'] = 'qt.qpa.application.debug=true'
command = ['qbs', 'run', 'profile:qt', 'config:debug',
           'projects.Tiled.nativeWidgetTests:true', '-p', 'tiled', '--',
           '--new-instance', str(pathlib.Path('examples/desert.tmx').resolve())]
log_path = out / 'production-app-console.txt'
with log_path.open('w') as log:
    process = subprocess.Popen(command, env=env, stdout=log,
                               stderr=subprocess.STDOUT, start_new_session=True)
    try:
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('Application exited before native window exposure')
            text = log_path.read_text()
            if 'attached-docks=' in text and any(
                    f'attached-docks={n}' in text for n in range(1, 30)):
                break
            time.sleep(0.5)
        else:
            raise RuntimeError('No active native dock material in production app log')
        # AppleEvents activate/quit the application, not Accessibility keystrokes.
        # Permission denial is an explicit test failure; do not bypass it.
        target = json.dumps(str(app))
        subprocess.run(['osascript', '-e', f'tell application {target} to activate'],
                       check=True, timeout=15)
        time.sleep(3)
        subprocess.run(['screencapture', '-x', str(out / 'production-app-desert.png')],
                       check=True, timeout=15)
        if process.poll() is not None:
            raise RuntimeError('Application exited before capture')
        quit_reply = subprocess.run(['osascript', '-e', f'tell application {target} to quit'],
                                    capture_output=True, text=True, timeout=15)
        (out / 'production-app-quit.txt').write_text(
            f'AppleEvent status={quit_reply.returncode}\n' + quit_reply.stdout + quit_reply.stderr)
        # Qt 6.8.3 QCocoaApplicationDelegate::applicationShouldTerminate returns
        # NSTerminateCancel even for an accepted quit, to unwind through main().
        # Only this known AppleEvent reply may proceed to the decisive exit check;
        # permissions and every other AppleEvent error still fail immediately.
        if quit_reply.returncode and '(-128)' not in quit_reply.stderr:
            raise subprocess.CalledProcessError(quit_reply.returncode, quit_reply.args,
                                                quit_reply.stdout, quit_reply.stderr)
        exit_code = process.wait(timeout=30)
        with (out / 'production-app-quit.txt').open('a') as result:
            result.write(f'Application process exit={exit_code}\n')
        if exit_code != 0:
            raise RuntimeError('Application did not close cleanly')
        text = log_path.read_text()
        if 'adding an unknown subview' in text:
            raise RuntimeError('AppKit rejected the native material hierarchy')
        (out / 'production-app-result.txt').write_text(
            'Actual built Tiled: sample map shown, native docks active, '
            'desktop captured, application exited zero after AppleEvent quit.\n')
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()

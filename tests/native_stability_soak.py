"""One continuously running native process exercises real editing for 15 minutes."""
import os
import pathlib
import signal
import subprocess

out = pathlib.Path(os.environ['TILED_WIDGET_ARTIFACTS']) / 'soak'
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['TILED_LONG_NATIVE_SOAK'] = '1'
env['QTEST_FUNCTION_TIMEOUT'] = '1100000'
command = ['qbs', 'run', 'profile:qt', 'config:debug',
           'projects.Tiled.nativeWidgetTests:true', '-p', 'test_layerwidgets', '--',
           '-o', str(out / 'tests.txt') + ',txt', '-maxwarnings', '1000000']
with (out / 'console.txt').open('w') as log:
    process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                               start_new_session=True)
    try:
        status = process.wait(timeout=1140)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        raise RuntimeError('Continuous native soak timed out')
    if status:
        raise subprocess.CalledProcessError(status, command)
if 'adding an unknown subview' in (out / 'console.txt').read_text():
    raise RuntimeError('AppKit rejected native view hierarchy')
(out / 'result.txt').write_text('PASS: one native test process, at least 900 seconds of repeated editing.\n')

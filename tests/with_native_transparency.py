"""Set a temporary native test preference ONLY on disposable GitHub macOS CI."""
import os
import pathlib
import subprocess
import sys

def require(condition, detail):
    if not condition:
        raise RuntimeError(str(detail))
require(sys.platform == 'darwin' and os.environ.get('GITHUB_ACTIONS') == 'true', 'Validation requirement failed')
preference = ['com.apple.universalaccess', 'reduceTransparency']
original = subprocess.run(['defaults', 'read', *preference], capture_output=True, text=True, timeout=15)
if original.returncode and 'does not exist' not in original.stderr:
    raise RuntimeError('Cannot inspect accessibility preference: ' + original.stderr)
old = original.stdout.strip() if original.returncode == 0 else None
require(old in (None, '0', '1'), old)
out = pathlib.Path(os.environ['TILED_WIDGET_ARTIFACTS'])
out.mkdir(parents=True, exist_ok=True)
(out / 'accessibility-environment.txt').write_text(f'Original reduceTransparency={old}\n')
try:
    subprocess.run(['defaults', 'write', *preference, '-bool', 'false'], check=True, timeout=15)
    subprocess.run(sys.argv[1:], check=True)
finally:
    if old is None:
        subprocess.run(['defaults', 'delete', *preference], check=True, timeout=15)
    else:
        subprocess.run(['defaults', 'write', *preference, '-bool', 'true' if old == '1' else 'false'], check=True, timeout=15)
    restored = subprocess.run(['defaults', 'read', *preference], capture_output=True, text=True, timeout=15)
    value = restored.stdout.strip() if restored.returncode == 0 else None
    with (out / 'accessibility-environment.txt').open('a') as log:
        log.write(f'Restored reduceTransparency={value}\n')
    require(value == old, 'Accessibility preference restoration mismatch')

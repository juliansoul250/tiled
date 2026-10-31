"""Check the vendored official importer against its pinned source inventory."""
import hashlib
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
base = ROOT / 'thirdparty/super-tiled2unity'
record = json.loads((base / 'SOURCE.json').read_text())
if record['commit'] != 'd3deae415963807caef14c522ed8be9bc30e89e0' or record['version'] != '2.4.0':
    raise RuntimeError('SuperTiled2Unity 固定来源不一致')
package = base / 'com.seanba.super-tiled2unity'
actual = {}
for path in package.rglob('*'):
    if path.is_symlink():
        raise RuntimeError('导入器包含符号链接')
    if path.is_file():
        actual[path.relative_to(package).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
if actual != record['files']:
    raise RuntimeError('导入器缺少文件或文件内容与固定来源不一致')
if not (base / 'UPSTREAM-LICENSE').is_file() or 'LICENSE.md' not in actual:
    raise RuntimeError('导入器许可证缺失')
print(f'固定官方 SuperTiled2Unity 2.4.0 来源检查通过：{len(actual)} 个文件。')

"""Require complete Simplified Chinese messages with intact substitutions."""
import collections
import os
import pathlib
import re
import xml.etree.ElementTree as ET

PLACEHOLDER = re.compile(r'%(?:\d*\.?\d*[sdfu]|L?(?:\d+|n))')
if PLACEHOLDER.findall('%1 %L1 %n %Ln %s %03d') != ['%1', '%L1', '%n', '%Ln', '%s', '%03d']:
    raise RuntimeError('格式占位符检查器自检失败')

root = pathlib.Path(__file__).resolve().parents[1]
ts = ET.parse(root / 'translations/tiled_zh_CN.ts').getroot()
errors = []
count = 0
for context in ts.findall('context'):
    for message in context.findall('message'):
        translation = message.find('translation')
        if translation is not None and translation.get('type') in ('obsolete', 'vanished'):
            continue
        count += 1
        source = message.findtext('source', '')
        translated = '' if translation is None else ''.join(translation.itertext())
        label = context.findtext('name', '') + ': ' + source
        if translation is None or translation.get('type') == 'unfinished' or not translated.strip():
            errors.append('未完成或空白译文：' + label)
        expected = collections.Counter(PLACEHOLDER.findall(source))
        forms = translation.findall('numerusform') if translation is not None else []
        for value in ([''.join(form.itertext()) for form in forms] if forms else [translated]):
            if collections.Counter(PLACEHOLDER.findall(value)) != expected:
                errors.append('格式占位符不一致：' + label)
            if collections.Counter(re.findall(r'</?(a|b|br|code|div|em|font|h[1-6]|i|img|li|ol|p|pre|span|strong|table|td|th|tr|u|ul)\b', source)) != collections.Counter(re.findall(r'</?(a|b|br|code|div|em|font|h[1-6]|i|img|li|ol|p|pre|span|strong|table|td|th|tr|u|ul)\b', value)):
                errors.append('HTML 标签不一致：' + label)
kit = os.environ.get('QT_ROOT_DIR')
if kit and not (pathlib.Path(kit) / 'translations/qtbase_zh_CN.qm').is_file():
    errors.append('Qt 套件缺少官方 qtbase_zh_CN.qm，不能把英文通用对话框当成中文验证通过。')
if errors:
    raise RuntimeError('\n'.join(errors))
print(f'简体中文翻译检查通过：{count} 条有效消息，无未完成/空白译文；格式占位符及标签保留。')

"""Build and validate static language resources; no firmware build or network access."""
import csv
import gzip
import hashlib
import html
import json
from pathlib import Path
import re
from html.parser import HTMLParser

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent
LOCALES = {'en': 'English', 'zh-CN': '简体中文'}
VERSION = '1.0.0'


def norm(value):
    return ' '.join(html.unescape(value).split())


def write_json(path, value):
    data = (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    path.write_bytes(data)
    return data


with (ROOT / 'messages.tsv').open(encoding='utf-8', newline='') as stream:
    rows = list(csv.DictReader(stream, delimiter='\t'))
keys = [r['key'] for r in rows]
assert len(keys) == len(set(keys)), 'Duplicate translation key'
assert all(re.fullmatch(r'[A-Za-z][A-Za-z0-9_.]*', k) for k in keys)
for row in rows:
    assert set(row) == {'key', *LOCALES}, f'Malformed row: {row}'
    for locale in LOCALES:
        assert row[locale].strip(), f'Empty translation: {row["key"]}'
        assert '<script' not in row[locale].lower()
    placeholders = lambda text: sorted(re.findall(r'\{[A-Za-z][A-Za-z0-9_]*\}', text))
    assert placeholders(row['en']) == placeholders(row['zh-CN']), row['key']

messages = {r['key']: r['en'] for r in rows}
english = {norm(v) for v in messages.values()}

# Trace the defined dynamic status, validator and menu texts to stable keys.
checked = {}
def require_texts(path, pattern):
    found = re.findall(pattern, path.read_text(encoding='utf-8'))
    missing = [v for v in found if norm(v) not in english]
    assert not missing, f'Missing text in {path.name}: {missing}'
    checked[str(path.relative_to(PROJECT))] = len(found)

app = PROJECT / 'ui/src/app'
require_texts(app / 'definitions/ble.ts', r"\w+:\s*'([^']+)'(?=[,\n])")
require_texts(app / 'definitions/homeAssistant.ts', r'description:\s*"([^"]+)"')
require_texts(app / 'definitions/obdStates.ts', r'description:\s*"([^"]+)"')
require_texts(app / 'components/obdStates.component.ts', r"return fail\('([^']+)'\)")
require_texts(app / 'components/bluetooth.component.ts', r"this\.error\s*=\s*'([^']+)'")
require_texts(app / 'app.component.ts', r'window\.confirm\("([^"]+)"\)')
for filename in ('settings.component.ts', 'obdStates.component.ts'):
    require_texts(app / 'components' / filename, r'text:\s*"([^"]+)"')

# Scan all static HTML text and placeholders. Angular expressions are dynamic;
# their known status/enum values are checked separately above and below.
class Texts(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.items = []
    def handle_data(self, text):
        text = norm(text).strip('} ').lstrip('⇵⇳⇩⇧⇅↑↓↧↥ ')
        if text and re.search('[A-Za-z]', text) and not any(t in text for t in ('@', '{{', '}}')):
            self.items.append(text)
    def handle_starttag(self, tag, attrs):
        is_button = tag == 'input' and dict(attrs).get('type') in ('submit', 'button', 'reset')
        for key, value in attrs:
            if key in ('placeholder', 'title', 'aria-label') or (key == 'value' and is_button):
                if value and re.search('[A-Za-z]', value) and '{{' not in value:
                    self.items.append(norm(value))

unmapped = []
for path in sorted(app.rglob('*.html')):
    parser = Texts()
    parser.feed(path.read_text(encoding='utf-8'))
    # Split paragraphs containing filename/link markup are represented by one
    # translation with those filenames kept literal; preserve protocol units.
    for value in parser.items:
        value = re.sub(r'^[^A-Za-z]+', '', value)
        if value not in english and not any(value in text for text in english):
            unmapped.append({'file': str(path.relative_to(PROJECT)), 'text': value})
assert not unmapped, f'Unmapped static HTML: {unmapped}'

for filename, enum_name, prefix, spaces in [
    ('settings.ts', 'NetworkMode', 'network', True),
    ('settings.ts', 'OBD2Protocol', 'protocol', True),
    ('settings.ts', 'MQTTIdentifierType', 'mqttIdentifier', True),
    ('settings.ts', 'MQTTProtocol', 'mqttProtocol', True),
    ('obdStates.ts', 'OBDStateType', 'stateType', False),
    ('obdStates.ts', 'OBDResponseFormat', 'responseFormat', False),
    ('obdStates.ts', 'ValueTypes', 'valueType', False),
    ('ota.ts', 'OTAMode', 'ota.mode', True),
]:
    text = (app / 'definitions' / filename).read_text(encoding='utf-8')
    body = re.search(r'export enum ' + enum_name + r'\s*\{([^}]+)\}', text).group(1)
    names = re.findall(r'^\s*([A-Z][A-Z0-9_]*)', body, re.M)
    for name in names:
        assert messages.get(prefix + '.' + name) == (name.replace('_', ' ') if spaces else name), name

error_codes = set()
for path in [PROJECT/'src/obd.cpp', PROJECT/'src/ble_connection.cpp',
             PROJECT/'vendor/BLESerial/src/BLESerial.cpp', PROJECT/'vendor/ELMDuino/src/ELMduino.cpp']:
    text = path.read_text(encoding='utf-8')
    error_codes.update(re.findall(r'(?:connectionError|lastError|lastInitError)\s*=\s*"([a-z_]+)"', text))
    error_codes.update(re.findall(r'return fail\("([a-z_]+)"\)', text))
for code in error_codes:
    assert 'ble.error.' + code in messages, code

catalog = {'schemaVersion': 1, 'version': VERSION, 'uiSchemaVersion': 1,
           'fallback': 'en', 'integrationStatus': 'pending', 'languages': []}
for locale, name in LOCALES.items():
    pack = {'schemaVersion': 1, 'version': VERSION, 'uiSchemaVersion': 1,
            'locale': locale, 'name': name, 'status': 'translated',
            'messages': {r['key']: r[locale] for r in rows}}
    data = write_json(ROOT / f'{locale}.json', pack)
    packed = gzip.compress(data, mtime=0)
    (ROOT / f'{locale}.json.gz').write_bytes(packed)
    catalog['languages'].append({'locale': locale, 'name': name, 'path': f'{locale}.json',
        'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
        'gzip': {'path': f'{locale}.json.gz', 'bytes': len(packed), 'sha256': hashlib.sha256(packed).hexdigest()}})
    assert gzip.decompress(packed) == data
    print(f'{locale}: {len(rows)} messages; JSON {len(data)} bytes; gzip {len(packed)} bytes')
write_json(ROOT / 'catalog.json', catalog)
print(f'Validated keys, placeholders, static HTML, enums, {len(error_codes)} backend error codes and {sum(checked.values())} dynamic source strings.')

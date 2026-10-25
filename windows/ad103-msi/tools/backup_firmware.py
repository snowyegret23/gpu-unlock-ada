import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / 'local/captures/CAP0003.JSON'
DESTINATION = ROOT / 'local/firmware-backups/ad103-CAP0003-ec33ad321f48'
CAPTURE_SHA256 = 'ab4a9381020852f3189f4f47a3faa0805e8d2fbb6d67cd1f5347701675e64f02'
BUILD_ID = 'e250d2d4030979ca844fda3ec61a86f3872f02d0cf1b2c7e517b3688390a772a'
FILES = ('CAP0003.JSON', 'physical-prom.bin', 'cached-efi-rom.bin')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def read_bounded(path, limit):
    with path.open('rb') as stream:
        raw = stream.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('File exceeds the expected size limit: ' + str(path))
    return raw


def extract(raw):
    if digest(raw) != CAPTURE_SHA256:
        raise ValueError('Source differs from the reviewed, complete CAP0003 capture')
    data = json.loads(raw)
    spec = importlib.util.spec_from_file_location('capture_analyze', ROOT / 'uefi/diagnostic/tools/analyze.py')
    analyzer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(analyzer)
    result = analyzer.analyze(data, BUILD_ID)
    if not result['identity_observed'] or not result['physical_prom_complete']:
        raise ValueError('A complete physical PROM capture of this GPU is required')
    target = next(d for d in data['display_devices'] if d['exact_expected_identity'])
    files = dict(zip(FILES, (raw, bytes.fromhex(data['physical_prom']['hex']),
                            bytes.fromhex(data['cached_rom_hex']))))
    metadata = {
        'schema': 1,
        'kind': 'offline-preservation-of-reviewed-read-only-capture',
        'capture_build_id': BUILD_ID,
        'gpu': {k: target[k] for k in ('vendor', 'device_id', 'subsystem_vendor',
                                      'subsystem_device', 'revision', 'location')},
        'physical_prom': {'file': FILES[1], 'source': 'GPU BAR0 PROM window',
                          'bar0_offset': data['physical_prom']['offset']},
        'cached_efi_rom': {'file': FILES[2], 'source': target['cached_efi_rom']['source']},
        'complete_flash_chip_backup_verified': False,
        'flash_restore_compatibility_verified': False,
        'files': {name: {'bytes': len(content), 'sha256': digest(content)}
                  for name, content in files.items()},
    }
    return files, metadata


def verify(folder):
    raw = read_bounded(folder / FILES[0], 8 * 1024 * 1024)
    files, expected = extract(raw)
    manifest = json.loads(read_bounded(folder / 'manifest.json', 16384))
    if not isinstance(manifest, dict):
        raise ValueError('Malformed backup manifest')
    created = manifest.pop('created_utc', None)
    if not isinstance(created, str) or not created.endswith('+00:00'):
        raise ValueError('Missing UTC backup timestamp')
    datetime.fromisoformat(created)
    if manifest != expected:
        raise ValueError('Backup metadata differs from the reviewed source')
    for name, content in files.items():
        if read_bounded(folder / name, len(content)) != content:
            raise ValueError('Backup bytes differ from the original capture: ' + name)
    return expected


def create(folder, capture=CAPTURE):
    if folder.exists():
        return verify(folder)
    files, manifest = extract(read_bounded(capture, 8 * 1024 * 1024))
    folder.mkdir(parents=True)
    for name, content in files.items():
        with (folder / name).open('xb') as stream:
            stream.write(content)
        if read_bounded(folder / name, len(content)) != content:
            raise ValueError('Backup readback failed: ' + name)
    manifest['created_utc'] = datetime.now(timezone.utc).isoformat()
    with (folder / 'manifest.json').open('x', encoding='utf-8', newline='') as stream:
        stream.write(json.dumps(manifest, ensure_ascii=False, indent=2).replace('\n', '\r\n') + '\r\n')
    return verify(folder)


def main():
    parser = argparse.ArgumentParser(description='Preserve/verify the reviewed GPU ROM capture offline; no GPU or firmware access.')
    parser.add_argument('action', choices=('create', 'verify'))
    parser.add_argument('--directory', type=Path, default=DESTINATION)
    parser.add_argument('--capture', type=Path, default=CAPTURE, help='Byte-exact reviewed CAP0003 input for a new backup')
    args = parser.parse_args()
    try:
        metadata = create(args.directory, args.capture) if args.action == 'create' else verify(args.directory)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, 'Backup failed: ' + str(error) + '\n')
    print('Verified backup: ' + str(args.directory.resolve()))
    for name, item in metadata['files'].items():
        print(f'{name}: {item["bytes"]} bytes; SHA256 {item["sha256"]}')
    print('Stored capture preserved byte-for-byte; GPU and boot settings were not accessed.')
    print('Full flash-chip coverage and restore-tool compatibility remain UNVERIFIED.')


if __name__ == '__main__':
    main()

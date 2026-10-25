"""Stage and schedule this machine's automatic one-attempt experimental EFI."""
import argparse
import ctypes as ct
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct

import probe_boot as probe
import backup_firmware as backup

ROOT, LOCAL = probe.ROOT, probe.LOCAL
PORT = ROOT / 'uefi/ad103'
BUILD = PORT / 'build'
STATE = LOCAL / 'unlock-boot.json'
BINARY = 'AD103-EXPERIMENTAL.EFI'
EFI_PATH = '\\EFI\\gpu-unlock\\' + BINARY
CAPTURE_HASH = 'ab4a9381020852f3189f4f47a3faa0805e8d2fbb6d67cd1f5347701675e64f02'
ASSETS = {
    'booter_load_ad102.bin': (57672, '6ee05b20e1dc244c08c052f36ef28469b8d09f4955d1fe33e48a179e11a7fd49'),
    'v67-ad102-610.43.03-four-plm.bin': (64000, '44c36b2d6b3424e53e18f3e55db85c833655de071ce66ce42749588a4a7dc8cb'),
    'gsp_ga10x.bin': (84277400, '572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a'),
    'gsp_boot_ad102.bin': (36864, '45d6335a8185d6ff13e8d1484cea73f68d3d13aa1c19dc080ed1c3482dd5bac0'),
    'gsp_boot_desc_ad102.bin': (84, '83f8f3f1f5f9496e0ea8fc7103c2beb64d0b6de2f5ffe725f13514de5a95fa42'),
    'NVIDIA-LICENSE': (22653, 'c692fb14ad4f499c6aee1b9e144e6905148c420e6225b17cb2937366c1e507ad'),
}


AUTO_DESCRIPTION = 'AD103 auto unlock (every boot)'


class Firmware(probe.Firmware):
    def set_boot_order(self, expected, desired):
        for order in (expected, desired):
            if not order or len(order) % 2 or len(order) > 512:
                raise ValueError('Invalid BootOrder length')
            numbers = struct.unpack('<' + 'H' * (len(order) // 2), order)
            if len(set(numbers)) != len(numbers):
                raise ValueError('Duplicate BootOrder entry')
        shorter, longer = sorted((expected, desired), key=len)
        if (len(longer) != len(shorter) + 2 or longer[2:] != shorter or
                not 0x8f80 <= struct.unpack('<H', longer[:2])[0] < 0x9000):
            raise ValueError('Only adding/removing one leading owned option is permitted')
        if self.read('BootOrder') != expected:
            raise ValueError('BootOrder changed before write')
        buffer = ct.create_string_buffer(desired)
        if not self.kernel.SetFirmwareEnvironmentVariableExW('BootOrder', probe.GLOBAL_GUID, buffer, len(desired), 7):
            raise ct.WinError(ct.get_last_error())
        if self.read('BootOrder') != desired:
            raise ValueError('Firmware variable readback mismatch: BootOrder')


def save_state(state):
    temporary = STATE.with_suffix('.json.pending')
    probe.save_json(temporary, state)
    temporary.replace(STATE)


def cancel_boot(firmware, state):
    name = state['boot_option']
    if name is None:
        if state.get('persistent'):
            raise ValueError('Persistent state has no owned option; inspect before changing firmware')
        return
    if not re.fullmatch(r'Boot8F[89A-F][0-9A-F]', name):
        raise ValueError('Invalid experimental option name')
    token = struct.pack('<H', int(name[4:], 16))
    original = bytes.fromhex(state['boot_order'])
    order = firmware.read('BootOrder')
    expected = bytes.fromhex(state['persistent_option_hex'] if state.get('persistent') else state['option_hex'])
    own = firmware.read(name)
    if own not in (None, expected):
        raise ValueError('Experimental option changed outside this tool')
    if state.get('persistent'):
        target = bytes.fromhex(state['persistent_order'])
        if target != token + original or token in [original[i:i+2] for i in range(0, len(original), 2)]:
            raise ValueError('Invalid saved persistent order')
        if order == target:
            firmware.set_boot_order(target, original)
        elif order != original:
            raise ValueError('BootOrder changed outside this tool; preserve it and inspect')
    order = firmware.read('BootOrder')
    if order is None or len(order) % 2 or token in [order[i:i+2] for i in range(0, len(order), 2)]:
        raise ValueError('Owned option is still referenced by BootOrder; refusing deletion')
    if firmware.read('BootNext') == token:
        firmware.write('BootNext', b'')
    own = firmware.read(name)
    if own == expected:
        firmware.write(name, b'')
    elif own is not None:
        raise ValueError('Experimental option changed during cancellation')
    state['boot_option'] = None
    for field in ('persistent', 'persistent_order', 'persistent_option_hex', 'windows_option_hex'):
        state.pop(field, None)
    save_state(state)


def experimental_option(validated_probe_option, description='AD103 automatic one-shot unlock'):
    flags, _, nodes = probe.decode_option(validated_probe_option)
    prefix = b''.join(node for kind, sub, node in nodes[:-1] if (kind, sub) != (4, 4))
    filename = (EFI_PATH + '\0').encode('utf-16-le')
    path = prefix + struct.pack('<BBH', 4, 4, len(filename) + 4) + filename + b'\x7f\xff\x04\x00'
    return struct.pack('<IH', flags, len(path)) + (description + '\0').encode('utf-16-le') + path


def verified_build():
    manifest = probe.read_json(BUILD / 'experimental-manifest.json')
    if (manifest.get('version') != '0.3.0' or manifest.get('binary') != BINARY or
            manifest.get('gpu_write_capability') is not True or manifest.get('hardware_tested') is not False or
            manifest.get('automatic_unlock') is not True or 'requires_key' not in manifest or
            manifest['requires_key'] is not None or manifest.get('automatic_delay_seconds') != 10):
        raise ValueError('Expected the reviewed automatic one-attempt experimental build')
    for path, field in ((LOCAL / 'profile.json', 'profile_sha256'),
                        (LOCAL / 'unlock-boot-profile.h', 'private_header_sha256')):
        if probe.digest(path.read_bytes()) != manifest[field]:
            raise ValueError('Local build profile changed: ' + path.name)
    backup.verify(backup.DESTINATION)
    if probe.digest((backup.DESTINATION / 'CAP0003.JSON').read_bytes()) != CAPTURE_HASH:
        raise ValueError('The reviewed hardware capture changed')
    for name, expected in manifest['source_hashes'].items():
        path = (PORT / name).resolve()
        if not path.is_relative_to(PORT.resolve()) or probe.digest(path.read_bytes()) != expected:
            raise ValueError('Source differs from build: ' + name)
    binary = (BUILD / BINARY).read_bytes()
    if probe.digest(binary) != manifest['sha256']:
        raise ValueError('EFI hash mismatch')
    validation = probe.read_json(BUILD / 'validation-manifest.json')
    if (validation['binary_sha256'] != manifest['sha256'] or
            validation['source_hashes'] != manifest['source_hashes'] or
            validation['profile_sha256'] != manifest['profile_sha256'] or
            validation['hardware_tested'] is not False):
        raise ValueError('Software validation does not match the built artifact')
    expected_reports = {'core-validation.json', 'ownership-validation.json',
                        'ownership-absolute-sibling-validation.json', 'ownership-single-child-validation.json',
                        'boot-io-validation.json',
                        'app-flow-validation.json'}
    if set(validation['report_sha256']) != expected_reports:
        raise ValueError('Missing validation report')
    for name, expected in validation['report_sha256'].items():
        raw = (BUILD / name).read_bytes()
        if probe.digest(raw) != expected:
            raise ValueError('Validation report changed: ' + name)
        report = json.loads(raw)
        results = [c['result'] for c in report['cases'].values()] if name == 'core-validation.json' else [report['result']]
        if not results or any(result != 0 for result in results):
            raise ValueError('Software validation failed: ' + name)
    spec = importlib.util.spec_from_file_location('unlock_pe', PORT / 'tools/pe_format.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.validate_pe(binary)
    return binary, manifest


def check_assets(folder):
    for name, (size, expected) in ASSETS.items():
        raw = (folder / name).read_bytes()
        if len(raw) != size or probe.digest(raw) != expected:
            raise ValueError('Firmware asset/notice mismatch: ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['status', 'stage', 'arm', 'cancel', 'collect', 'enable-auto', 'disable-auto'])
    args = parser.parse_args()
    firmware = Firmware()
    if args.action in ('cancel', 'disable-auto'):
        state = probe.read_json(STATE)
        profile = probe.read_json(LOCAL / 'profile.json')
        candidate = probe.option_for_probe(firmware.read(state['windows_option']), profile)
        if state['option_hex'] not in (experimental_option(candidate).hex(),
                                       experimental_option(candidate, 'AD103 experimental unlock - press U').hex()):
            raise ValueError('Saved Windows/ESP identity no longer matches')
        if state.get('persistent') and state['persistent_option_hex'] != experimental_option(candidate, AUTO_DESCRIPTION).hex():
            raise ValueError('Saved persistent option no longer matches this Windows/ESP')
        cancel_boot(firmware, state)
        print('Automatic/one-time unlock disabled. Owned option removed; Windows boot order restored if needed.')
        return
    profile, volume = probe.environment()
    folder = volume / 'EFI/gpu-unlock'
    order, entries, candidate = probe.inventory(firmware, profile)
    option = experimental_option(candidate[1])
    next_boot = firmware.read('BootNext')
    if args.action == 'status':
        staged = probe.read_json(folder / 'manifest.json') if (folder / 'manifest.json').exists() else {}
        state = probe.read_json(STATE) if STATE.exists() else {}
        name = state.get('boot_option')
        enabled = bool(state.get('persistent') and name and entries[0]['option'] == name and
                       firmware.read(name) == experimental_option(candidate[1], AUTO_DESCRIPTION))
        print(json.dumps({'every_boot_enabled': enabled,
                          'boot_order_matches_saved': order.hex() == state.get('persistent_order', state.get('boot_order')),
                          'boot_order': entries, 'boot_next': next_boot.hex() if next_boot else None,
                          'experimental_staged': (folder / BINARY).exists(),
                          'windows_boot_manager_unchanged': True,
                          'staged_version': staged.get('version'),
                          'automatic_unlock': staged.get('automatic_unlock'),
                          'requires_key': staged.get('requires_key')}, indent=2))
        return
    if args.action == 'collect':
        destination = LOCAL / 'unlock-runs'
        destination.mkdir(exist_ok=True)
        paths = sorted((folder / 'logs').glob('RUN????.TXT'))
        if not paths:
            raise ValueError('No experimental run logs exist yet')
        for path in paths:
            raw = path.read_bytes()
            target = destination / path.name
            if target.exists() and target.read_bytes() != raw:
                raise ValueError('Saved run log differs; preserve both before collecting again: ' + path.name)
            if not target.exists():
                with target.open('xb') as stream:
                    stream.write(raw)
            print(path.name + ': ' + str(len(raw)) + ' bytes; SHA256 ' + probe.digest(raw))
            print(raw.decode('ascii', errors='replace')[-6000:])
        return
    if args.action == 'stage':
        if next_boot is not None:
            raise ValueError('Another one-time boot is scheduled')
        binary, manifest = verified_build()
        assets = LOCAL / 'assets-610.43.03'
        check_assets(assets)
        if folder.exists() or STATE.exists():
            state = probe.read_json(STATE)
            legacy = experimental_option(candidate[1], 'AD103 experimental unlock - press U').hex()
            if (state['boot_option'] is not None or state['boot_order'] != order.hex() or
                    state['option_hex'] not in (option.hex(), legacy)):
                raise ValueError('Inspect/cancel the existing experiment before replacing it')
            if probe.digest((folder / BINARY).read_bytes()) != state['sha256']:
                raise ValueError('Existing EFI changed outside this tool')
            check_assets(folder / 'assets')
            archive = LOCAL / 'unlock-history' / state['sha256']
            archive.mkdir(parents=True, exist_ok=True)
            for name in (BINARY, 'manifest.json'):
                raw = (folder / name).read_bytes()
                old = archive / name
                if old.exists() and old.read_bytes() != raw:
                    raise ValueError('Archived experiment differs: ' + name)
                if not old.exists():
                    old.write_bytes(raw)
        else:
            required = len(binary) + sum(size for size, _ in ASSETS.values()) + 4 * 1024 * 1024
            if shutil.disk_usage(volume).free < required:
                raise ValueError('Insufficient free space for assets and logs')
            folder.mkdir()
            (folder / 'logs').mkdir()
            (folder / 'assets').mkdir()
            for name in ASSETS:
                with (folder / 'assets' / name).open('xb') as stream:
                    stream.write((assets / name).read_bytes())
        (folder / BINARY).write_bytes(binary)
        probe.save_json(folder / 'manifest.json', manifest)
        check_assets(folder / 'assets')
        if probe.digest((folder / BINARY).read_bytes()) != manifest['sha256']:
            raise ValueError('Staged EFI readback mismatch')
        save_state({'boot_order': order.hex(), 'windows_option': candidate[0],
                               'option_hex': option.hex(), 'sha256': manifest['sha256'], 'boot_option': None})
        if firmware.read('BootOrder') != order or firmware.read('BootNext') != next_boot:
            raise ValueError('Boot state changed during staging; inspect before any restart')
        probe.environment()
        print('Staged automatic one-attempt EFI/assets. No boot variable or GPU changes; no reboot requested.')
        return
    state = probe.read_json(STATE)
    binary, manifest = verified_build()
    check_assets(folder / 'assets')
    if (probe.digest((folder / BINARY).read_bytes()) != probe.digest(binary) or state['sha256'] != manifest['sha256'] or
            probe.read_json(folder / 'manifest.json') != manifest):
        raise ValueError('Staged build differs from the validated artifact')
    if firmware.read('SecureBoot') != b'\0':
        raise ValueError('Secure Boot must already be disabled')
    if args.action == 'enable-auto' and state.get('persistent'):
        name = state['boot_option']
        if (name and re.fullmatch(r'Boot8F[89A-F][0-9A-F]', name) and next_boot is None and
                order.hex() == state['persistent_order'] and
                order == struct.pack('<H', int(name[4:], 16)) + bytes.fromhex(state['boot_order']) and
                firmware.read(name) == experimental_option(candidate[1], AUTO_DESCRIPTION) and
                state['persistent_option_hex'] == firmware.read(name).hex() and
                option.hex() == state['option_hex']):
            print('Every-boot unlock is already enabled; no firmware changes.')
            return
        raise ValueError('Persistent boot state changed; inspect or disable before enabling again')
    if next_boot is not None or state['boot_option'] is not None or state.get('persistent') or order.hex() != state['boot_order'] or option.hex() != state['option_hex']:
        raise ValueError('Boot state changed or an experimental option is already armed')
    for number in range(0x8f80, 0x9000):
        name = f'Boot{number:04X}'
        if firmware.read(name) is None:
            break
    else:
        raise ValueError('No unused experimental option slot')
    windows = firmware.read(candidate[0])
    state['boot_option'] = name
    if args.action == 'enable-auto':
        option = experimental_option(candidate[1], AUTO_DESCRIPTION)
        state.update(persistent=True, persistent_order=(struct.pack('<H', number) + order).hex(),
                     persistent_option_hex=option.hex(), windows_option_hex=windows.hex())
    save_state(state)
    try:
        if firmware.read(name) is not None or firmware.read('BootNext') is not None or firmware.read('BootOrder') != order:
            raise ValueError('Boot variables changed before setup')
        firmware.write(name, option)
        if args.action == 'enable-auto':
            firmware.set_boot_order(order, bytes.fromhex(state['persistent_order']))
            if firmware.read('BootNext') is not None:
                raise ValueError('BootNext changed during automatic setup')
        else:
            firmware.write('BootNext', struct.pack('<H', number))
            if firmware.read('BootOrder') != order:
                raise ValueError('BootOrder changed; inspect before restarting')
        if firmware.read(candidate[0]) != windows:
            raise ValueError('Windows boot option changed during setup')
        probe.environment()
    except Exception:
        cancel_boot(firmware, state)
        raise
    if args.action == 'enable-auto':
        print(f'Enabled {name} before Windows on every boot. 10s without input; S/Esc skips. No reboot requested.')
        return
    print(f'Armed {name} for one boot. One attempt starts after 10s without input; S/Esc cancels. No reboot requested.')


if __name__ == '__main__':
    main()

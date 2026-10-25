import argparse
import ctypes as ct
from ctypes import wintypes as wt
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import uuid

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / 'local'
BUILD = ROOT / 'uefi/diagnostic/build'
GLOBAL_GUID = '{8be4df61-93ca-11d2-aa0d-00e098032b8c}'
EFI_PATH = '\\EFI\\gpu-unlock-probe\\AD103-PROBE.EFI'
STATE = LOCAL / 'probe-boot.json'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save_json(path, value):
    with path.open('w', encoding='utf-8', newline='\r\n') as stream:
        stream.write(json.dumps(value, indent=2) + '\n')


def load_module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'uefi/diagnostic/tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Firmware:
    def __init__(self):
        self.kernel = ct.WinDLL('kernel32', use_last_error=True)
        advapi = ct.WinDLL('advapi32', use_last_error=True)

        class Luid(ct.Structure):
            _fields_ = [('low', wt.DWORD), ('high', wt.LONG)]

        class Privileges(ct.Structure):
            _fields_ = [('count', wt.DWORD), ('luid', Luid), ('attributes', wt.DWORD)]

        self.kernel.GetCurrentProcess.restype = wt.HANDLE
        self.kernel.CloseHandle.argtypes = [wt.HANDLE]
        advapi.OpenProcessToken.argtypes = [wt.HANDLE, wt.DWORD, ct.POINTER(wt.HANDLE)]
        advapi.LookupPrivilegeValueW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ct.POINTER(Luid)]
        advapi.AdjustTokenPrivileges.argtypes = [wt.HANDLE, wt.BOOL, ct.POINTER(Privileges), wt.DWORD, ct.c_void_p, ct.c_void_p]
        token = wt.HANDLE()
        if not advapi.OpenProcessToken(self.kernel.GetCurrentProcess(), 0x28, ct.byref(token)):
            raise ct.WinError(ct.get_last_error())
        try:
            privilege = Privileges(count=1, attributes=2)
            if not advapi.LookupPrivilegeValueW(None, 'SeSystemEnvironmentPrivilege', ct.byref(privilege.luid)):
                raise ct.WinError(ct.get_last_error())
            ct.set_last_error(0)
            if not advapi.AdjustTokenPrivileges(token, False, ct.byref(privilege), 0, None, None) or ct.get_last_error():
                raise ct.WinError(ct.get_last_error())
        finally:
            self.kernel.CloseHandle(token)
        self.kernel.GetFirmwareEnvironmentVariableExW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ct.c_void_p, wt.DWORD, ct.POINTER(wt.DWORD)]
        self.kernel.GetFirmwareEnvironmentVariableExW.restype = wt.DWORD
        self.kernel.SetFirmwareEnvironmentVariableExW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ct.c_void_p, wt.DWORD, wt.DWORD]
        self.kernel.SetFirmwareEnvironmentVariableExW.restype = wt.BOOL

    def read(self, name):
        data = ct.create_string_buffer(65536)
        attributes = wt.DWORD()
        size = self.kernel.GetFirmwareEnvironmentVariableExW(name, GLOBAL_GUID, data, len(data), ct.byref(attributes))
        if not size:
            error = ct.get_last_error()
            if error in (2, 203):
                return None
            raise ct.WinError(error)
        return data.raw[:size]

    def write(self, name, data):
        if not (name == 'BootNext' or re.fullmatch(r'Boot8F[0-9A-F]{2}', name)):
            raise ValueError('Only BootNext and this tool\'s reserved option range can be changed')
        buffer = ct.create_string_buffer(data) if data else None
        if not self.kernel.SetFirmwareEnvironmentVariableExW(name, GLOBAL_GUID, buffer, len(data), 7):
            raise ct.WinError(ct.get_last_error())
        expected = data if data else None
        if self.read(name) != expected:
            raise ValueError('Firmware variable readback mismatch: ' + name)


def decode_option(data):
    if data is None or len(data) < 12:
        raise ValueError('Missing or truncated firmware load option')
    flags, length = struct.unpack_from('<IH', data)
    end = 6
    while end + 2 <= len(data) and data[end:end + 2] != b'\0\0':
        end += 2
    if end + 2 + length > len(data) or not length:
        raise ValueError('Invalid load option lengths')
    description = data[6:end].decode('utf-16-le')
    path = data[end + 2:end + 2 + length]
    offset = 0
    nodes = []
    while offset + 4 <= len(path):
        kind, subtype, size = struct.unpack_from('<BBH', path, offset)
        if size < 4 or offset + size > len(path):
            raise ValueError('Invalid device path node')
        node = path[offset:offset + size]
        nodes.append((kind, subtype, node))
        offset += size
        if kind == 0x7f:
            if subtype != 0xff or size != 4 or offset != len(path):
                raise ValueError('Only a single, terminated device path is supported')
            return flags, description, nodes
    raise ValueError('Device path is not terminated')


def option_for_probe(original, profile):
    flags, _, nodes = decode_option(original)
    if not flags & 1:
        raise ValueError('Windows boot option is inactive')
    prefix = b''
    hd = []
    file_paths = []
    for kind, subtype, node in nodes[:-1]:
        if (kind, subtype) == (4, 4):
            if len(node) < 6 or node[-2:] != b'\0\0':
                raise ValueError('Invalid Windows loader path')
            file_paths.append(node[4:-2].decode('utf-16-le'))
        else:
            if file_paths:
                raise ValueError('Unexpected node after Windows file path')
            prefix += node
            if (kind, subtype) == (4, 1):
                hd.append(node)
    if len(hd) != 1 or len(hd[0]) != 42 or [p.lower() for p in file_paths] != ['\\efi\\microsoft\\boot\\bootmgfw.efi']:
        raise ValueError('Expected the existing Windows Boot Manager on one GPT partition')
    node = hd[0]
    partition, start, size = struct.unpack_from('<IQQ', node, 4)
    if node[40:] != b'\x02\x02' or node[24:40] != uuid.UUID(profile['esp_guid']).bytes_le or (partition, start, size) != (profile['esp_partition'], profile['esp_start_lba'], profile['esp_size_lba']):
        raise ValueError('Windows EFI partition does not match local/profile.json')
    filename = (EFI_PATH + '\0').encode('utf-16-le')
    path = prefix + struct.pack('<BBH', 4, 4, len(filename) + 4) + filename + b'\x7f\xff\x04\x00'
    return struct.pack('<IH', 1, len(path)) + 'AD103 read-only diagnostic\0'.encode('utf-16-le') + path


def environment():
    profile = read_json(LOCAL / 'profile.json')
    if profile.get('schema') != 1 or profile.get('device') != '10de:2705' or profile.get('subsystem') != '1462:513d':
        raise ValueError('Unexpected hardware profile')
    volume = Path('\\\\?\\Volume{' + str(uuid.UUID(profile['esp_guid'])) + '}\\')
    if str(volume).rstrip('\\').lower() != profile['volume_path'].rstrip('\\').lower():
        raise ValueError('Volume/GPT identity mismatch')
    boot_manager = volume / 'EFI/Microsoft/Boot/bootmgfw.efi'
    if digest(boot_manager.read_bytes()) != profile['windows_boot_manager_sha256']:
        raise ValueError('Windows Boot Manager changed since configuration')
    return profile, volume


def verified_build(profile):
    manifest = read_json(BUILD / 'manifest.json')
    if manifest.get('gpu_write_capability') is not False or manifest.get('hardware_tested') is not False or manifest.get('version') != '0.5.2':
        raise ValueError('Expected a diagnostic-only build')
    if manifest['profile_sha256'] != digest((LOCAL / 'profile.json').read_bytes()):
        raise ValueError('Build does not match this machine profile')
    binary = (BUILD / 'AD103-PROBE.EFI').read_bytes()
    if digest(binary) != manifest['sha256']:
        raise ValueError('EFI binary hash mismatch')
    for name, expected in manifest['source_sha256'].items():
        if Path(name).name != name or digest((BUILD.parent / 'src' / name).read_bytes()) != expected:
            raise ValueError('Source changed after build: ' + name)
    load_module('verify').validate_pe(binary)
    return binary, manifest


def inventory(firmware, profile):
    order = firmware.read('BootOrder')
    if order is None or len(order) % 2 or len(order) > 512:
        raise ValueError('Unexpected firmware BootOrder')
    entries = []
    candidate = None
    for number in struct.unpack('<' + 'H' * (len(order) // 2), order):
        name = f'Boot{number:04X}'
        data = firmware.read(name)
        _, description, _ = decode_option(data)
        entries.append({'option': name, 'description': description})
        try:
            option = option_for_probe(data, profile)
        except ValueError:
            continue
        if candidate is not None:
            raise ValueError('Multiple matching Windows boot options')
        candidate = (name, option)
    if candidate is None:
        raise ValueError('No matching Windows firmware boot option')
    return order, entries, candidate


def capture_summary(raw, build_id):
    if not raw:
        return {'capture_complete': False, 'reason': 'empty_file'}
    data = json.loads(raw.decode('utf-8-sig'))
    if data.get('schema') != 4 or data.get('probe_version') not in ('0.5.0', '0.5.1', '0.5.2') or data.get('build_id') != build_id:
        raise ValueError('Unexpected capture version or build identity')
    for field in ('gpu_writes', 'unlock_attempted', 'hardware_support_validated'):
        if data.get(field) is not False:
            raise ValueError('Unexpected diagnostic flag: ' + field)
    if data['probe_version'] != '0.5.0' and data.get('capture_complete') is False:
        stage = data.get('last_completed_stage')
        if stage not in ('started', 'boot_variables', 'pci_inventory', 'display_topology', 'registers', 'physical_prom', 'cached_rom'):
            raise ValueError('Unexpected incomplete checkpoint')
        return {'capture_complete': False, 'last_completed_stage': stage, 'unlock_ready': False}
    result = load_module('analyze').analyze(data, build_id)
    result.pop('stable_fingerprint', None)
    result['capture_complete'] = True
    return result


def status_summary(raw, build_id):
    data = json.loads(raw.decode('utf-8-sig'))
    if data.get('schema') != 1 or data.get('probe_version') != '0.5.2' or data.get('build_id') != build_id:
        raise ValueError('Unexpected status log identity')
    if data.get('gpu_writes') is not False or data.get('unlock_attempted') is not False:
        raise ValueError('Unexpected status log flags')
    if data.get('stage') not in ('started', 'boot_variables', 'pci_inventory', 'display_topology', 'registers', 'physical_prom', 'cached_rom', 'finished'):
        raise ValueError('Unexpected status log stage')
    if data.get('operation') not in ('serialize', 'seek', 'write', 'short_write', 'flush', 'close'):
        raise ValueError('Unexpected status log operation')
    if not re.fullmatch(r'0x[0-9a-f]{16}', data.get('efi_status', '')):
        raise ValueError('Malformed EFI result')
    for name, maximum in (('write_offset', 8 * 1024 * 1024), ('requested', 32768), ('transferred', 32768)):
        value = data.get(name)
        if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= maximum:
            raise ValueError('Malformed status log count: ' + name)
    return {**{k: data[k] for k in ('stage', 'operation', 'efi_status', 'write_offset', 'requested', 'transferred')},
            'status_log_only': True, 'unlock_ready': False}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['status', 'stage', 'arm', 'cancel', 'collect'])
    args = parser.parse_args()
    profile, volume = environment()
    folder = volume / 'EFI/gpu-unlock-probe'
    firmware = Firmware()
    order, entries, candidate = inventory(firmware, profile)
    next_boot = firmware.read('BootNext')
    if args.action == 'status':
        print(json.dumps({'boot_order': entries, 'boot_next': next_boot.hex() if next_boot else None,
                          'diagnostic_staged': (folder / 'AD103-PROBE.EFI').exists(),
                          'windows_boot_manager_unchanged': True}, indent=2))
        return
    if args.action == 'stage':
        if next_boot is not None:
            raise ValueError('Another one-time boot is already scheduled')
        binary, manifest = verified_build(profile)
        replacing = STATE.exists() or folder.exists()
        if replacing:
            previous = read_json(STATE)
            old_manifest = read_json(folder / 'manifest.json')
            old_binary = (folder / 'AD103-PROBE.EFI').read_bytes()
            if previous['boot_option'] is not None or previous['boot_order'] != order.hex() or previous['option_hex'] != candidate[1].hex():
                raise ValueError('Cancel the existing diagnostic option and inspect the boot state before restaging')
            if digest(old_binary) != previous['sha256'] or old_manifest['sha256'] != previous['sha256'] or old_manifest['build_id'] != previous['build_id']:
                raise ValueError('Existing diagnostic files changed; refusing to replace them')
            if not re.fullmatch('[0-9a-f]{64}', previous['build_id']):
                raise ValueError('Invalid previous build identity')
            archive = LOCAL / 'probe-history' / previous['build_id']
            archive.mkdir(parents=True, exist_ok=True)
            for name in ('AD103-PROBE.EFI', 'manifest.json'):
                raw = (folder / name).read_bytes()
                target = archive / name
                if target.exists() and target.read_bytes() != raw:
                    raise ValueError('Previous build archive differs: ' + name)
                if not target.exists():
                    with target.open('xb') as stream:
                        stream.write(raw)
        else:
            folder.mkdir()
            (folder / 'captures').mkdir()
        with (folder / 'AD103-PROBE.EFI').open('wb' if replacing else 'xb') as stream:
            stream.write(binary)
        save_json(folder / 'manifest.json', manifest)
        state = {'boot_order': order.hex(), 'windows_option': candidate[0], 'option_hex': candidate[1].hex(),
                 'sha256': manifest['sha256'], 'build_id': manifest['build_id'], 'boot_option': None}
        save_json(STATE, state)
        if digest((folder / 'AD103-PROBE.EFI').read_bytes()) != manifest['sha256'] or firmware.read('BootOrder') != order or firmware.read('BootNext') != next_boot:
            raise ValueError('Staging verification failed')
        print('Staged the verified read-only diagnostic. BootOrder and BootNext are unchanged.')
        return
    state = read_json(STATE)
    if args.action == 'arm':
        binary, manifest = verified_build(profile)
        if digest((folder / 'AD103-PROBE.EFI').read_bytes()) != digest(binary) or manifest['sha256'] != state['sha256']:
            raise ValueError('Staged EFI differs from the verified build')
        if firmware.read('SecureBoot') != b'\0':
            raise ValueError('Unsigned diagnostic requires Secure Boot already disabled')
        if next_boot is not None or order.hex() != state['boot_order'] or candidate[1].hex() != state['option_hex']:
            raise ValueError('Firmware boot state changed or BootNext is already set')
        if state['boot_option'] is not None:
            raise ValueError('This diagnostic already has an option; inspect/cancel it before rearming')
        for number in range(0x8f00, 0x9000):
            name = f'Boot{number:04X}'
            if firmware.read(name) is None:
                break
        else:
            raise ValueError('No unused diagnostic option number')
        state['boot_option'] = name
        save_json(STATE, state)
        try:
            firmware.write(name, bytes.fromhex(state['option_hex']))
            firmware.write('BootNext', struct.pack('<H', number))
            if firmware.read('BootOrder') != order:
                raise ValueError('BootOrder changed unexpectedly; inspect before rebooting')
        except Exception:
            if firmware.read('BootNext') == struct.pack('<H', number):
                firmware.write('BootNext', b'')
            if firmware.read(name) == bytes.fromhex(state['option_hex']):
                firmware.write(name, b'')
            state['boot_option'] = None
            save_json(STATE, state)
            raise
        print(f'Scheduled {name} for one diagnostic boot. BootOrder is unchanged. No reboot was requested.')
    elif args.action == 'cancel':
        name = state['boot_option']
        if name is None:
            print('No diagnostic boot option has been created.')
            return
        number = int(name[4:], 16)
        own = firmware.read(name)
        if own not in (None, bytes.fromhex(state['option_hex'])):
            raise ValueError('Diagnostic option was changed externally')
        if next_boot == struct.pack('<H', number):
            firmware.write('BootNext', b'')
        if own is not None:
            firmware.write(name, b'')
        state['boot_option'] = None
        save_json(STATE, state)
        print('Removed only this diagnostic option and its pending BootNext, if present.')
    else:
        destination = LOCAL / 'captures'
        destination.mkdir(exist_ok=True)
        captures = sorted((folder / 'captures').glob('CAP????.JSON')) + sorted((folder / 'captures').glob('STA????.JSON'))
        if not captures:
            raise ValueError('No firmware captures found; a diagnostic boot is still required')
        for path in captures:
            if not re.fullmatch(r'(CAP|STA)\d{4}\.JSON', path.name) or not 0 <= path.stat().st_size <= 8 * 1024 * 1024:
                raise ValueError('Invalid capture filename or size')
            raw = path.read_bytes()
            target = destination / path.name
            if target.exists() and target.read_bytes() != raw:
                raise ValueError('Local capture with different contents already exists')
            if not target.exists():
                with target.open('xb') as stream:
                    stream.write(raw)
            try:
                build_id = state['build_id']
                if raw:
                    data = json.loads(raw.decode('utf-8-sig'))
                    if not isinstance(data, dict):
                        raise ValueError('Expected a capture object')
                    captured_build = data.get('build_id')
                    if captured_build != build_id:
                        if not isinstance(captured_build, str) or not re.fullmatch('[0-9a-f]{64}', captured_build):
                            raise ValueError('Invalid captured build identity')
                        old = read_json(LOCAL / 'probe-history' / captured_build / 'manifest.json')
                        if old.get('build_id') != captured_build or old.get('gpu_write_capability') is not False:
                            raise ValueError('Capture is not from an archived diagnostic build')
                        build_id = captured_build
                result = status_summary(raw, build_id) if path.name.startswith('STA') else capture_summary(raw, build_id)
            except (ValueError, KeyError, TypeError, FileNotFoundError) as error:
                result = {'capture_complete': False, 'reason': 'validation_failed', 'detail': str(error), 'unlock_ready': False}
            print(path.name, json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()

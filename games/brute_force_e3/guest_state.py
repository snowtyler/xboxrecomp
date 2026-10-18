"""Read-only observations of statically traced Brute Force E3 state.

The callback reads guest virtual addresses, not host addresses. No guest code
is called and no thread is stopped. See GUEST_STATE_EVIDENCE.md for provenance
and the difference between a failure flag, a request and completed replay.
"""
from typing import Callable

SCHEMA_VERSION = 1
XBE_SHA256 = 'bcf4fbfb1adc595a3d04fe1443f22fcc1ec61bee4909f2780783d6ba0a13c4de'
CAMPAIGN_TYPE = 0x159BEC67
INTERFACE_VTABLE = 0x003B69C0


def snapshot_game_state(read: Callable[[int, int], bytes], *, include_dumps=False):
    """Return bounded observations; unavailable/transitioning objects stay unknown.

    A null pointer during loading is ordinary. Read errors are reported within
    this result, so optional game state cannot invalidate the main GPU snapshot.
    Reads are not atomic; roots are read again to detect replacement during
    collection. Stable roots do not imply an atomic whole-game snapshot.
    """
    result = {'schema_version': SCHEMA_VERSION, 'source_xbe_sha256': XBE_SHA256,
              'semantics': 'static-reader mapping; runtime transition validation pending',
              'death_flag': None, 'replay_entered': None, 'errors': {}}

    def checked(address, size):
        if size < 0 or size > 4096 or not (
                0x10000 <= address <= 0x04000000 - size or
                0x80000000 <= address <= 0x84000000 - size):
            raise ValueError(f'Guest range out of bounds: {address:#x}+{size:#x}')
        data = read(address, size)
        if len(data) != size:
            raise ValueError('Short guest-state read')
        return data

    def uint(address, size=4):
        return int.from_bytes(checked(address, size), 'little')

    def observe(name, operation):
        try:
            operation()
        except (OSError, ValueError, OverflowError) as error:
            result['errors'][name] = str(error)

    def campaign():
        manager = uint(0x00480998)
        result['manager_pointer'] = manager
        if not manager:
            return
        obj = uint(manager + 0xC34)
        result['campaign_pointer'] = obj
        if obj:
            kind = uint(obj + 0x14)
            flags = uint(obj + 0x24, 1)
            result.update(campaign_type=kind, campaign_flags=flags)
            if kind == CAMPAIGN_TYPE:
                result['death_flag'] = bool(flags & 2)
        result['campaign_roots_stable'] = (
            uint(0x00480998) == manager and uint(manager + 0xC34) == obj)
        if not result['campaign_roots_stable']:
            result['death_flag'] = None
        mission = uint(manager + 0xBD4)
        result['mission_state_pointer'] = mission
        if mission:
            method = uint(uint(mission) + 0x34)
            result['mission_state_reader'] = {
                'address': method, 'bytes': checked(method, 16).hex()}

    def interface():
        root = uint(0x00440370)
        result['application_pointer'] = root
        if not root:
            return
        obj = uint(root + 8)
        result['interface_pointer'] = obj
        if not obj:
            return
        vtable = uint(obj)
        result['interface_vtable'] = vtable
        if include_dumps:
            result['interface_prefix'] = checked(obj, 0x80).hex()
            result['interface_vtable_bytes'] = checked(vtable, 0x240).hex()
        if vtable != INTERFACE_VTABLE:
            result['errors']['interface_layout'] = 'Unrecognized game interface vtable'
            return
        result['interface_world_pointer'] = uint(obj + 0x5C0)
        result['interface_flags'] = checked(obj + 0x5BC, 2).hex()
        queue = obj + 0x6C8
        slots, capacity, first, count = (uint(queue + n) for n in (4, 8, 12, 16))
        result['transition_queue'] = {'capacity': capacity, 'first': first, 'count': count,
                                      'requests': [], 'truncated': count > 8}
        if count:
            if not 0 < capacity <= 4096 or first >= capacity or count > capacity:
                raise ValueError('Transition queue changed or has invalid bounds')
            # Consumer takes newest request first; retain that same order.
            for n in range(min(count, 8)):
                index = (first + count - 1 - n) % capacity
                item = uint(slots + index * 4)
                raw = checked(item, 20)
                result['transition_queue']['requests'].append({
                    'address': item, 'flags': raw[0] & 0x1F, 'bytes': raw.hex()})
        result['interface_roots_stable'] = (
            uint(0x00440370) == root and uint(root + 8) == obj and
            uint(queue + 12) == first and uint(queue + 16) == count)

    def network():
        obj = uint(0x00485560)
        result['network_pointer'] = obj
        result['network_active'] = bool(uint(obj + 0x1C)) if obj else None

    def swaps():
        obj = uint(0x003AB378)
        result['device_pointer'] = obj
        result['sdk_swap_serial'] = uint(obj + 0x2018) if obj else None

    # Bind semantic reads to the observed failure-flag writer's original bytes.
    try:
        result['code_signature_matches'] = checked(0x0009152A, 4) == bytes.fromhex('804e2402')
    except (OSError, ValueError, OverflowError) as error:
        result['code_signature_matches'] = False
        result['errors']['identity'] = str(error)
    if not result['code_signature_matches']:
        return result
    for name, operation in [('campaign', campaign), ('interface', interface),
                            ('network', network), ('swaps', swaps)]:
        observe(name, operation)
    return result

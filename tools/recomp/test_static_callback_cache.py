"""Repeated boundary proofs must retain fresh callback protection."""
from . import config
from .translator import FunctionTranslator


BASE = 0x10000
TARGET = BASE + 0x100


def make_translator(monkeypatch):
    monkeypatch.setattr(config, '_SECTIONS', [
        config.Section('.text', BASE, 0x200, 0, 0x200, True)])
    db = {a: {'_addr': a, 'start': hex(a), 'end': a + 4,
              'section': '.text', 'called_by': []} for a in (BASE, TARGET)}
    t = FunctionTranslator(b'\xc3'.ljust(0x100, b'\x90') +
                           b'\xc3'.ljust(0x100, b'\x90'), db)
    return t


def test_new_callback_evidence_is_not_cached(monkeypatch):
    t = make_translator(monkeypatch)
    monkeypatch.setattr(t, '_find_static_indirect_ranges',
                        lambda ins: [(0x30000, 0x30004)]
                        if ins[0].address == BASE else [])
    targets = []
    monkeypatch.setattr(t, '_read_static_callback_table',
                        lambda *args: list(targets))
    t.discover_static_indirect_targets(coalescing=True)
    assert t.func_db[TARGET]['called_by'] == []
    targets.append(TARGET)
    t.discover_static_indirect_targets(coalescing=True)
    assert t.func_db[TARGET]['called_by'] == ['0x00010000']


def test_extent_change_and_recovered_body_are_reanalyzed(monkeypatch):
    t = make_translator(monkeypatch)
    decoded = []
    original = t.disasm.disassemble_function

    def decode(raw, start, end):
        decoded.append((start, end))
        return original(raw, start, end)

    monkeypatch.setattr(t.disasm, 'disassemble_function', decode)
    monkeypatch.setattr(t, '_find_static_indirect_ranges', lambda ins: [])
    t.discover_static_indirect_targets()
    t.discover_static_indirect_targets()
    assert len(decoded) == 2
    t.func_db[BASE]['end'] += 1
    t.discover_static_indirect_targets()
    assert len(decoded) == 3
    current = original(t.xbe_data[:5], BASE, BASE + 5)
    t._recovered_cfg[BASE] = {'end': BASE + 5, 'instructions': current}
    analyzed = []
    monkeypatch.setattr(t, '_find_static_indirect_ranges',
                        lambda ins: analyzed.append(ins) or [])
    t.discover_static_indirect_targets()
    assert analyzed == [current]

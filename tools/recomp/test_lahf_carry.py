"""Arithmetic followed by LAHF must declare and compute the captured carry."""
import unittest
from tools.recomp import config
from tools.recomp.translator import FunctionTranslator


def translate_arithmetic_lahf(operation):
    # add eax,edx / add al,dl; lahf; ret. There is no Jcc/ADC to otherwise
    # request carry tracking, which previously produced uncompilable C.
    image = bytes.fromhex(operation + "9fc3")
    base = 0x10000
    config._install([config.Section(".text", base, len(image), 0, len(image), True)],
                    entry_point=base, kernel_thunk_addr=base, origin="lahf-carry-test")
    database = {base: {"start": hex(base), "end": base + len(image),
                       "_addr": base, "size": len(image)}}
    return FunctionTranslator(image, database).translate_function(base, database[base])


class TestLahfCarry(unittest.TestCase):
    def test_arithmetic_lahf_tracks_carry(self):
        for operation in ("01d0", "00d0"):
            with self.subTest(operation=operation):
                code = translate_arithmetic_lahf(operation)
                self.assertIn("int _cf = 0;", code)
                self.assertIn("_cf = (int)(", code)
                self.assertIn("((_cf) ? 0x01u : 0u)", code)
                self.assertLess(code.index("_cf = (int)("), code.index("/* lahf (add) */"))

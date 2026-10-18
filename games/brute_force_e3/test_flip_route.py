import json
from pathlib import Path
import struct
import tempfile
import unittest
from validate_flip_route import specification, summarize


class FlipRouteTests(unittest.TestCase):
    def test_ranges_and_wrap_are_checked_before_launch(self):
        self.assertEqual(specification('f10:a:2','10,20-22')[1],[10,20,21,22])
        for script,captures in [('f10:a:0','10'),('f4294967295:a:2','10'),
                                ('f10:a:2','20-10'),('f10:a:2','0'),('f10:a:2','1-1001')]:
            with self.assertRaises(ValueError):specification(script,captures)

    def test_exact_input_and_complete_bitmap_receipts_required(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent/'analysis') as name:
            prefix=Path(name)/'probe'
            def side(suffix):return prefix.with_name(prefix.name+suffix)
            side('-process.json').write_text(json.dumps({'configuration':{
                'RECOMP_PAD_SCRIPT':'f10:a:2','RECOMP_FB_DUMP_GUEST_FLIPS':'20',
                'RECOMP_PAD_SCRIPT_EXCLUSIVE':'1'}}))
            good='PAD: t=10 flips step 0 (digital 0x00 analog 0x01) flip=10\n[FRAME CAPTURE] flip=20 surface=00184000\n'
            side('.stderr.log').write_text(good)
            bitmap=bytearray(58);bitmap[:2]=b'BM';struct.pack_into('<ii',bitmap,18,1,1)
            image=side('-frame-guest-flip-0000000020.bmp');image.write_bytes(bitmap)
            self.assertTrue(summarize(prefix)['flip_route_checks_passed'])
            side('.stderr.log').write_text(good.replace('flip=10','flip=11'))
            self.assertFalse(summarize(prefix)['flip_route_checks_passed'])
            side('.stderr.log').write_text(good);image.write_bytes(bitmap[:-1])
            self.assertFalse(summarize(prefix)['flip_route_checks_passed'])


if __name__=='__main__':unittest.main()

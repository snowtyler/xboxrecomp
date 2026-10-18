"""Synthetic evidence must not let a replay menu or pointer change pass a gate."""
import copy
from pathlib import Path
import tempfile
import unittest
from install_guest_state_hooks import HOOKS, install
from validate_guest_route import qualify, PREPARE, ACCEPT, CONSUME, RELOAD
from guest_state import XBE_SHA256


class GuestRouteTests(unittest.TestCase):
    def evidence(self):
        def sample(t, dead, campaign=100, swaps=1):
            return dict(monotonic_seconds=t, state=dict(code_signature_matches=True,
                source_xbe_sha256=XBE_SHA256,
                campaign_roots_stable=True, campaign_pointer=campaign,
                death_flag=dead, sdk_swap_serial=swaps))
        def event(t, pc):
            return dict(qpc=t*1000, qpc_hz=1000, pc=pc, flags=6,
                        interface=1000, request=2000, payload=[6,1,2,3,4])
        return [sample(1,False),sample(2,True),sample(6,False,300,10),sample(7,False,300,11)], \
            [event(2.5,PREPARE),event(3,ACCEPT),event(4,CONSUME),event(5,RELOAD)], \
            [dict(pc=0x111D43,qpc=2000,qpc_hz=1000,campaign=100,predicate=1)]

    def test_complete_evidence_still_requires_capture_review(self):
        result=qualify(*self.evidence())
        self.assertTrue(result['guest_state_checks_passed'])
        self.assertIn('pending_capture_control_review',result['status'])

    def test_every_transition_checkpoint_and_full_predicate_required(self):
        samples,route,deaths=self.evidence()
        for index in range(4):
            self.assertFalse(qualify(samples,route[:index]+route[index+1:],deaths)['guest_state_checks_passed'])
        self.assertFalse(qualify(samples,route,[])['guest_state_checks_passed'])
        self.assertFalse(qualify(samples[1:],route,deaths)['guest_state_checks_passed'])
        self.assertFalse(qualify(samples[:-1],route,deaths)['guest_state_checks_passed'])

    def test_unrelated_request_and_wrong_order_fail(self):
        samples,route,deaths=self.evidence()
        unrelated=copy.deepcopy(route)
        unrelated[2].update(request=9000,payload=[6,9,9,9,9])
        self.assertFalse(qualify(samples,unrelated,deaths)['guest_state_checks_passed'])
        early=copy.deepcopy(route);early[1]['qpc']=1000
        self.assertFalse(qualify(samples,early,deaths)['guest_state_checks_passed'])
        # A copied descriptor retains payload identity across allocation.
        copied=copy.deepcopy(route);copied[2]['request']=9000
        self.assertTrue(qualify(samples,copied,deaths)['guest_state_checks_passed'])

    def test_installer_is_idempotent_and_validates_before_writing(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent/'analysis') as name:
            root=Path(name)
            for address,hooks in HOOKS.items():
                (root/f'recomp_{address}.c').write_text(f'void sub_{address}(void)\n{{\n'+'\n'.join(hooks)+'\n}\n')
            install(root)
            first={p:p.read_text() for p in root.glob('*.c')}
            install(root);self.assertEqual(first,{p:p.read_text() for p in root.glob('*.c')})
            target=root/'recomp_0008A350.c'
            target.write_text(target.read_text().replace('loc_0008A4CF: ;','missing'))
            before={p:p.read_text() for p in root.glob('*.c')}
            with self.assertRaises(ValueError):install(root)
            self.assertEqual(before,{p:p.read_text() for p in root.glob('*.c')})


if __name__=='__main__':unittest.main()

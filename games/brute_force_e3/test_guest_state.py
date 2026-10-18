"""Guest-state reads must stay bounded and avoid false transition claims."""
import unittest
from guest_state import snapshot_game_state, CAMPAIGN_TYPE, INTERFACE_VTABLE


class GuestStateTests(unittest.TestCase):
    def fixture(self):
        memory = {}
        def put(address, value, size=4):
            memory.update({address+i: b for i,b in enumerate(value.to_bytes(size,'little'))})
        def read(address, size):
            return bytes(memory.get(address+i, 0) for i in range(size))
        for i,b in enumerate(bytes.fromhex('804e2402')):memory[0x9152A+i]=b
        put(0x480998,0x1000000);put(0x1000C34,0x1100000)
        put(0x1100014,CAMPAIGN_TYPE);put(0x1100024,2,1)
        put(0x440370,0x1200000);put(0x1200008,0x1300000)
        put(0x1300000,INTERFACE_VTABLE)
        put(0x3AB378,0x1400000);put(0x1402018,123)
        return put,read

    def test_death_requires_correct_type_and_never_implies_replay(self):
        put,read=self.fixture()
        result=snapshot_game_state(read)
        self.assertTrue(result['death_flag'])
        self.assertEqual(result['sdk_swap_serial'],123)
        self.assertIsNone(result['replay_entered'])
        put(0x1100014,0)
        self.assertIsNone(snapshot_game_state(read)['death_flag'])

    def test_wrong_code_signature_rejects_semantic_reads(self):
        put,read=self.fixture();put(0x9152A,0)
        result=snapshot_game_state(read)
        self.assertFalse(result['code_signature_matches'])
        self.assertNotIn('manager_pointer',result)

    def test_invalid_transition_queue_does_not_follow_arbitrary_slots(self):
        put,read=self.fixture()
        put(0x13006D0,0x100000);put(0x13006D8,9)
        result=snapshot_game_state(read)
        self.assertIn('interface',result['errors'])
        self.assertEqual(result['transition_queue']['requests'],[])

    def test_reload_request_is_observation_not_completed_replay(self):
        put,read=self.fixture()
        put(0x13006CC,0x1500000);put(0x13006D0,1);put(0x13006D8,1)
        put(0x1500000,0x1600000);put(0x1600000,6,1)
        result=snapshot_game_state(read)
        self.assertEqual(result['transition_queue']['requests'][0]['flags'],6)
        self.assertIsNone(result['replay_entered'])


if __name__=='__main__':unittest.main()

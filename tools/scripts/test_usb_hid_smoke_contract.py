import json
import unittest
from unittest.mock import Mock
from smoke_x64_qemu_usb_hid import qmp_execute


class UsbHidSmokeContract(unittest.TestCase):
    def test_key_uses_qmp_and_ignores_async_event(self):
        stream = Mock()
        stream.readline.side_effect = [b'{"event":"RESET"}\n',
            b'{"id":"send-key","return":{}}\n']
        arguments = {"keys": [{"type": "qcode", "data": "a"}], "hold-time": 100}
        self.assertEqual(qmp_execute(stream, "send-key", arguments), {})
        request = json.loads(stream.write.call_args.args[0])
        self.assertEqual(request, {"execute": "send-key", "id": "send-key",
                                   "arguments": arguments})

    def test_error_cannot_count_as_key_sent(self):
        stream = Mock()
        stream.readline.return_value = b'{"id":"send-key","error":{"desc":"failed"}}\n'
        with self.assertRaises(RuntimeError):
            qmp_execute(stream, "send-key")

    def test_mismatched_reply_is_bounded(self):
        stream = Mock()
        stream.readline.return_value = b'{"id":"other","return":{}}\n'
        with self.assertRaises(RuntimeError):
            qmp_execute(stream, "send-key")
        self.assertEqual(stream.readline.call_count, 64)

    def test_closed_or_oversized_reply_is_rejected(self):
        for response in (b"", b"x" * 65537):
            stream = Mock()
            stream.readline.return_value = response
            with self.assertRaises(RuntimeError):
                qmp_execute(stream, "send-key")


if __name__ == "__main__":
    unittest.main()

import json
import unittest
from migration_bridge_policy import resolve_policy
from update_manifest_common import ManifestError

FINAL, BRIDGE = "0.11.3+20261004", "0.11.2+20261004"
POLICY = json.dumps(dict(final_version=FINAL, bridge_version=BRIDGE))
HEADER = f'#define CAPYOS_RUNTIME_VERSION_FULL "{BRIDGE}"\n'


class BridgePolicyTests(unittest.TestCase):
    def test_exact_release_requires_the_runtime_identity(self):
        self.assertEqual(resolve_policy(POLICY, HEADER, FINAL), BRIDGE)
        for header in ("", HEADER.replace(BRIDGE, FINAL), HEADER + HEADER):
            with self.assertRaises(ManifestError):
                resolve_policy(POLICY, header, FINAL)

    def test_historical_and_future_releases_do_not_inherit_bridge(self):
        self.assertEqual(resolve_policy(None, "", "0.11.1+20261004"), "")
        for version in ("0.11.4+20261005", "0.12.0-alpha.1+20261005", "0.11.3+20261005"):
            self.assertEqual(resolve_policy(POLICY, HEADER, version), "")

    def test_malformed_policy_is_not_silently_disabled(self):
        for raw in ("bad", "[]", "{}", POLICY.replace(BRIDGE, FINAL),
                    POLICY.replace('"bridge_version"', '"unknown"'),
                    POLICY.replace('{', '{"final_version":"0.11.3+20261004",', 1)):
            with self.assertRaises(ManifestError):
                resolve_policy(raw, HEADER, FINAL)


if __name__ == "__main__":
    unittest.main()

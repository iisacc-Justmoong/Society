from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    "tst_notification_center.cpp": ("preservesVisibleAndActiveState", "existingShellCanBindToTheAdapter"),
    "tst_recent_backend.cpp": ("switchesWithoutPublishingStaleResultsAndPersistsOldDriveActivity", "failedWritesAreRetainedForRetry"),
    "tst_resident_gateway.cpp": ("independentRequestsAndDriveChange", "slowConsumersHaveABoundedQueue"),
}
TARGETS = ("SocietyNotificationCenterTests", "SocietyRecentBackendTests", "SocietyResidentGatewayTests")

class RecentGatewayNotificationCTestContract(unittest.TestCase):
    def test_original_cases_and_signal_checks_remain(self):
        for file, cases in CASES.items():
            source = (ROOT / "tests" / file).read_text()
            with self.subTest(file=file):
                self.assertEqual(re.findall(r'runner.run\("([^\"]+)"', source), list(cases))
                self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy)\b|\bQTEST_|<QTest>|<QtTest|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")
        notification = (ROOT / "tests/tst_notification_center.cpp").read_text()
        for name in ("counts", "inserted", "removed"):
            self.assertIn(f"++{name}", notification)
        recent = (ROOT / "tests/tst_recent_backend.cpp").read_text()
        self.assertIn("qScopeGuard", recent)
        self.assertIn("gateReleased", recent)
        gateway = (ROOT / "tests/tst_resident_gateway.cpp").read_text()
        self.assertIn("received.append", gateway)

    def test_targets_do_not_link_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        for target in TARGETS:
            match = re.search(r"target_link_libraries\(" + target + r"\s+([^)]*)\)", cmake)
            self.assertIsNotNone(match)
            self.assertNotIn("Qt6::Test", match.group(1))
        self.assertIn("Society.RecentGatewayNotificationCTestContract", cmake)

if __name__ == "__main__":
    unittest.main()

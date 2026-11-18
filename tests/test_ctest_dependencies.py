from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CASES = (
    "fileProjectionHidesProviderControlFiles",
    "packagedApplicationObservesPeers",
    "directDependenciesMatchProductFeatures",
)


class DependencyCTestContract(unittest.TestCase):
    def test_canvas_core_uses_the_exact_sdk_version(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        declaration = re.search(
            r"find_package\(iiSharedCanvas\s+0\.35\.39\s+EXACT\s+CONFIG\s+REQUIRED\s+COMPONENTS\s+Core\b", cmake
        )
        self.assertIsNotNone(declaration, "Society must request iiSharedCanvas 0.35.39 EXACT Core")
        self.assertIsNotNone(
            re.search(r'set\(society_canvas_version "\$\{iiSharedCanvas_VERSION\}"\)', cmake),
            "Record the Canvas version before finding Helper",
        )
        self.assertIsNotNone(
            re.search(r'if\(NOT iiSocietyHelper_CANVAS_VERSION STREQUAL society_canvas_version\)', cmake),
            "Helper must export the same Canvas version as Society",
        )

    def test_cases_and_wait_remain(self):
        source = (ROOT / "tests/tst_dependencies.cpp").read_text()
        self.assertEqual(re.findall(r'runner.run\("([^"]+)"', source), list(CASES))
        self.assertIn("society_ctest::processFor(50)", source)
        self.assertIn("app.waitForStarted()", source)
        self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy)\b|\bQTEST_|<QTest>|<QtTest|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")

    def test_target_and_offline_selection(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        link = re.search(r"target_link_libraries\(SocietyDependencyTests\s+([^)]*)\)", cmake)
        self.assertIsNotNone(link)
        self.assertNotIn("Qt6::Test", link.group(1))
        self.assertIn("Society.DependenciesOffline", cmake)
        self.assertIn("Society.DependenciesCTestContract", cmake)
        self.assertIn("Society.CTestEventWait", cmake)


if __name__ == "__main__":
    unittest.main()

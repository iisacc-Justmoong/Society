from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SUITES = {
    "native_files_launcher": ("SocietyNativeFilesLauncherTests", ("startsOnlyTheExpectedExecutable",)),
    "containerfileopen": ("SocietyContainerFileOpenTests", (
        "nativeOpenWaitsForTheControllerAndThenOpensOnePackage",
        "accountLinkWaitsForWindowAndRejectsUnexpectedParameters",
        "ignoresOrdinaryFoldersAndRemoteUrls",
    )),
}


class NativeEntryCTestContract(unittest.TestCase):
    def test_cases_and_fixture_remain(self):
        for suite, (_, cases) in SUITES.items():
            source = (ROOT / f"tests/tst_{suite}.cpp").read_text()
            for case in cases:
                self.assertIn(f'runner.run("{case}"', source)
            self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy)\b|\bQTEST_|<QTest>|<QtTest|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")
        launcher = (ROOT / "tests/tst_native_files_launcher.cpp").read_text()
        self.assertIn('SOCIETY_TEST_DIRECTORY)+"/native-launcher-', launcher)
        self.assertIn("auto start=", launcher)

    def test_targets_do_not_link_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        for target, _ in SUITES.values():
            link = re.search(r"target_link_libraries\(" + target + r"\s+([^)]*)\)", cmake)
            self.assertIsNotNone(link)
            self.assertNotIn("Qt6::Test", link.group(1))
        self.assertIn("Society.NativeEntryCTestContract", cmake)


if __name__ == "__main__":
    unittest.main()

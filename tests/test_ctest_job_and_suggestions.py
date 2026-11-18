from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SUITES = {
    "jobstorage": ("SocietyJobStorageTests", ("reopensAndProtectsAnInterruptedWorker",)),
    "suggested_model": ("SocietySuggestedModelTests", (
        "requestsBoundedLocalEstimatesWithoutTools",
        "rejectsMalformedAndDuplicateEstimates",
        "choosesAnEligibleModelByDecision",
    )),
}


class JobAndSuggestionsCTestContract(unittest.TestCase):
    def test_cases_and_exceptions_are_preserved(self):
        for suite, (_, cases) in SUITES.items():
            source = (ROOT / f"tests/tst_{suite}.cpp").read_text()
            self.assertEqual(re.findall(r'runner.run\("([^"]+)"', source), list(cases))
            self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy)\b|\bQTEST_|<QTest>|<QtTest|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")
        self.assertIn("CTEST_THROWS(std::runtime_error", (ROOT / "tests/tst_suggested_model.cpp").read_text())
        self.assertIn("FixtureModel", (ROOT / "tests/tst_suggested_model.cpp").read_text())

    def test_targets_do_not_link_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        for target, _ in SUITES.values():
            link = re.search(r"target_link_libraries\(" + target + r"\s+([^)]*)\)", cmake)
            self.assertIsNotNone(link)
            self.assertNotIn("Qt6::Test", link.group(1))
        self.assertIn("Society.JobAndSuggestionsCTestContract", cmake)


if __name__ == "__main__":
    unittest.main()

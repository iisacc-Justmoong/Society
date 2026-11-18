from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CASES = (
    "previousEncryptedRecordsAreCopiedWithoutChangingTheSource",
    "boundedLargeAccountSnapshotSurvivesAndOversizeDoesNotReplaceIt",
    "ciphertextSurvivesRestartAndSeparatesRecords",
    "migratesOnlyAfterSuccessfulEncryptedWrite",
    "damagedOrSubstitutedCiphertextNeverFallsBack",
    "logoutInvalidatesPendingWriteFromAnotherStore",
    "refusesRedirectedDirectoriesAndFiles",
    "anOlderProcessCannotSaveAgainAfterAnotherProcessLogsOut",
    "cryptoUsesFreshNoncesAndAuthenticatesContext",
)


class GroupStateCTestContract(unittest.TestCase):
    def test_cases_and_async_callback_are_preserved(self):
        source = (ROOT / "tests/tst_groupstate.cpp").read_text()
        for case in CASES:
            self.assertIn(f'runner.run("{case}"', source)
        self.assertIn("writtenResult", source)
        self.assertIn("FlushDeferredDeletes", source)
        self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy)\b|\bQTEST_|<QTest>|<QtTest|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")

    def test_product_target_does_not_link_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        link = re.search(r"target_link_libraries\(SocietyGroupStateTests\s+([^)]*)\)", cmake)
        self.assertIsNotNone(link)
        self.assertNotIn("Qt6::Test", link.group(1))
        self.assertIn("Society.GroupStateCTestContract", cmake)


if __name__ == "__main__":
    unittest.main()

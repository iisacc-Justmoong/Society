#include "CTestSupport.h"

int main()
{
    unsigned seen = 0;
    society_ctest::Runner selected("second");
    selected.run("first", [&] { seen |= 1; });
    selected.run("second", [&] { seen |= 2; });
    if (seen != 2 || selected.result() != 0) return 1;

    society_ctest::Runner missing("missing");
    missing.run("known", [] {});
    if (missing.result() == 0) return 1;

    society_ctest::Runner failing;
    failing.run("failure", [] { CTEST_COMPARE(1, 2); });
    bool messagePreserved = false;
    try { CTEST_CHECK_MESSAGE(false, "expected diagnostic"); }
    catch (const std::runtime_error& error) {
        messagePreserved = std::string(error.what()).find("expected diagnostic") != std::string::npos;
    }
    CTEST_THROWS(std::runtime_error, throw std::runtime_error("expected"));
    bool missingExceptionDetected = false;
    try { CTEST_THROWS(std::runtime_error, (void)0); }
    catch (const std::runtime_error&) { missingExceptionDetected = true; }
    return failing.result() == 0 || !messagePreserved || !missingExceptionDetected ? 1 : 0;
}

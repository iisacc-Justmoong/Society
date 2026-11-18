#include "QtEventWait.h"
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    bool delivered = false;
    QTimer::singleShot(0, [&] { delivered = true; });
    society_ctest::processFor(5);
    if (!delivered || !society_ctest::waitFor([] { return true; }, 0)) return 1;
    if (society_ctest::waitFor([] { return false; }, 0)) return 1;
    int value = 0;
    QTimer::singleShot(0, [&] { value = 42; });
    CTEST_WAIT_COMPARE_TIMEOUT(value, 42, 1000);
    CTEST_WAIT_VERIFY(value == 42, 0);
    bool timedOut = false;
    try { CTEST_WAIT_VERIFY(value == 0, 0); }
    catch (const std::runtime_error&) { timedOut = true; }
    return timedOut ? 0 : 1;
}

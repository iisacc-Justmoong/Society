#include "AccountController.h"
#ifndef SOCIETY_HEADLESS
#include <iiAcountManager/Quick/AccountViews.h>
#include <QCoreApplication>
static void registerSocietyAccountViews() { iisacc::accounts::registerAccountViews(); }
Q_COREAPP_STARTUP_FUNCTION(registerSocietyAccountViews)
#endif

#pragma once
class QObject;
namespace society::native { class OverlayShutdown; }
void installSocietyMcp(QObject* root, QObject* lifetime, society::native::OverlayShutdown* shutdown);

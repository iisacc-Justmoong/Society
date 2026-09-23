#pragma once
#include "App/Network/NetworkDriveController.h"

// Multi-device fixtures run both native roles in one desktop test process.
// The separately compiled ClientOnlyNetwork target verifies the mobile default.
class MobileNetworkDevice : public NetworkDriveController {
public:
    explicit MobileNetworkDevice(QObject *parent = nullptr)
        : NetworkDriveController(ClientMode, nullptr, QHostAddress::AnyIPv4, parent) {}
    MobileNetworkDevice(DiscoveryService *service, QHostAddress bindAddress, QObject *parent = nullptr)
        : NetworkDriveController(ClientMode, service, bindAddress, parent) {}
};

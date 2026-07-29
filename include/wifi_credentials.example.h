#pragma once

#include "config.h"

// Copy this file to wifi_credentials.h and add up to three preferred networks.
// wifi_credentials.h is intentionally ignored by Git so passwords are never
// committed. Leave unused entries as empty strings.
namespace config {

constexpr WifiCredential kDefaultWifiCredentials[] = {
    {"YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD"},
    {"", ""},
    {"", ""},
};

constexpr size_t kDefaultWifiCredentialCount =
    sizeof(kDefaultWifiCredentials) / sizeof(kDefaultWifiCredentials[0]);

}  // namespace config

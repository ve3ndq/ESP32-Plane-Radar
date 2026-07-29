#pragma once

/** True when the next boot should show the setup screen first (after credential reset). */
bool wifiShowsSetupScreenOnBoot();
void wifiResetCredentialsAndReboot();
/** Boot flow: try local defaults and saved credentials, then open setup AP. */
bool wifiSetupConnect();
/** Reconnect using local defaults and saved credentials; never opens the captive portal. */
bool wifiReconnect();
/** Keeps the LAN config portal alive; call every loop() iteration. */
void wifiLoop();
bool wifiBootButtonPressed();
/** GPIO + interrupt setup; call once early in setup(). */
void bootButtonInit();
/** Latched short tap (survives blocking HTTP/display work). */
bool bootButtonConsumeTap();
/** Call each loop iteration; triggers WiFi reset on long hold. */
void bootButtonPollLongPress();

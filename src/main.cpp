/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/display_settings.h"
#include "services/ota_update.h"
#include "services/radar_location.h"
#include "services/weather_time.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
TaskHandle_t g_network_task = nullptr;
portMUX_TYPE g_network_state_mux = portMUX_INITIALIZER_UNLOCKED;
bool g_network_data_dirty = false;

struct NetworkInputs {
  double latitude = 0.0;
  double longitude = 0.0;
  float fetch_radius_km = 0.0f;
};

NetworkInputs g_network_inputs;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  services::weather::begin();
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void updateNetworkInputs() {
  NetworkInputs inputs;
  inputs.latitude = services::location::lat();
  inputs.longitude = services::location::lon();
  inputs.fetch_radius_km = ui::radar::fetchRadiusKm();
  portENTER_CRITICAL(&g_network_state_mux);
  g_network_inputs = inputs;
  portEXIT_CRITICAL(&g_network_state_mux);
}

NetworkInputs networkInputsSnapshot() {
  portENTER_CRITICAL(&g_network_state_mux);
  const NetworkInputs inputs = g_network_inputs;
  portEXIT_CRITICAL(&g_network_state_mux);
  return inputs;
}

void markNetworkDataDirty() {
  portENTER_CRITICAL(&g_network_state_mux);
  g_network_data_dirty = true;
  portEXIT_CRITICAL(&g_network_state_mux);
}

bool consumeNetworkDataDirty() {
  portENTER_CRITICAL(&g_network_state_mux);
  const bool dirty = g_network_data_dirty;
  g_network_data_dirty = false;
  portEXIT_CRITICAL(&g_network_state_mux);
  return dirty;
}

void logNetworkTiming(const char* operation, unsigned long started_ms,
                      bool changed) {
  const unsigned long elapsed_ms = millis() - started_ms;
  Serial.printf(
      "timing: %-10s %lu ms%s, heap %u, largest %u, stack margin %u\n",
      operation, elapsed_ms, changed ? " (updated)" : "",
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMaxAllocHeap()),
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void networkTask(void*) {
  unsigned long last_adsb_fetch_ms = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED || services::ota::inProgress()) {
      vTaskDelay(pdMS_TO_TICKS(config::kNetworkTaskIdleMs));
      continue;
    }

    const NetworkInputs inputs = networkInputsSnapshot();
    const unsigned long now = millis();
    if (last_adsb_fetch_ms == 0 ||
        now - last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      last_adsb_fetch_ms = now;
      const unsigned long started_ms = millis();
      const bool changed = services::adsb::fetchUpdate(
          inputs.latitude, inputs.longitude, inputs.fetch_radius_km);
      logNetworkTiming("ADS-B", started_ms, changed);
      if (changed) {
        markNetworkDataDirty();
      }
    } else {
      unsigned long started_ms = millis();
      const bool weather_changed = services::weather::refreshIfDue(
          inputs.latitude, inputs.longitude);
      const unsigned long weather_elapsed_ms = millis() - started_ms;
      if (weather_changed || weather_elapsed_ms >= 25) {
        logNetworkTiming("weather", started_ms, weather_changed);
      }
      if (weather_changed) {
        markNetworkDataDirty();
      }

      started_ms = millis();
      const bool enrichment_changed = services::adsb::enrichOnePending();
      const unsigned long enrichment_elapsed_ms = millis() - started_ms;
      if (enrichment_changed || enrichment_elapsed_ms >= 25) {
        logNetworkTiming("enrichment", started_ms, enrichment_changed);
      }
      if (enrichment_changed) {
        markNetworkDataDirty();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(config::kNetworkTaskIdleMs));
  }
}

void startNetworkTask() {
  if (g_network_task != nullptr) {
    return;
  }
  const BaseType_t created =
      xTaskCreate(networkTask, "radar-network", config::kNetworkTaskStackBytes,
                  nullptr, 1, &g_network_task);
  if (created != pdPASS) {
    g_network_task = nullptr;
    Serial.println("network: background task creation failed");
  } else {
    Serial.printf("network: task started, heap %u, largest block %u\n",
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  services::settings::init();
  updateNetworkInputs();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
  startNetworkTask();
}

void loop() {
  handleBootButton();
  wifiLoop();
  updateNetworkInputs();

  if (services::ota::inProgress()) {
    delay(10);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else {
      if (consumeNetworkDataDirty()) {
        ui::radarDisplayRefreshAircraft();
      } else {
        ui::radarDisplayRefreshSweep();
      }
    }
  }

  delay(10);
}

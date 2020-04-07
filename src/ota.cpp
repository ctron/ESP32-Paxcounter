#if (USE_OTA)

/*
 Parts of this code:
 Copyright (c) 2014-present PlatformIO <contact@platformio.org>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#include "ota.h"

using namespace std;

// Local logging tag
static const char TAG[] = __FILE__;

void start_ota_update() {

  switch_LED(LED_ON);

// init display
#ifdef HAS_DISPLAY

  dp_setup();

  dp_printf(0, 0, 0, 1, "SOFTWARE UPDATE");
  dp_printf(0, 1, 0, 0, "WiFi connect  ..");
  dp_printf(0, 2, 0, 0, "Has Update?   ..");
  dp_printf(0, 3, 0, 0, "Fetching      ..");
  dp_printf(0, 4, 0, 0, "Downloading   ..");
  dp_printf(0, 5, 0, 0, "Rebooting     ..");
  dp_dump(displaybuf);
#endif

  ESP_LOGI(TAG, "Starting Wifi OTA update");
  ota_display(1, "**", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint8_t i = WIFI_MAX_TRY;
  int ret = 1; // 0 = finished, 1 = retry, -1 = abort

  while (i--) {
    ESP_LOGI(TAG, "Trying to connect to %s, attempt %u of %u", WIFI_SSID,
             WIFI_MAX_TRY - i, WIFI_MAX_TRY);
    delay(10000); // wait for stable connect
    if (WiFi.status() == WL_CONNECTED) {
      // we now have wifi connection and try to do an OTA over wifi update
      ESP_LOGI(TAG, "Connected to %s", WIFI_SSID);
      ota_display(1, "OK", "WiFi connected");
      // do a number of tries to update firmware limited by OTA_MAX_TRY
      uint8_t j = OTA_MAX_TRY;
      while ((j--) && (ret > 0)) {
        ESP_LOGI(TAG, "Starting OTA update, attempt %u of %u", OTA_MAX_TRY - j,
                 OTA_MAX_TRY);
        ret = do_ota_update();
      }
      if (WiFi.status() == WL_CONNECTED)
        goto end; // OTA update finished or OTA max attemps reached
    }
    WiFi.reconnect();
  }

  // wifi did not connect
  ESP_LOGI(TAG, "Could not connect to %s", WIFI_SSID);
  ota_display(1, " E", "no WiFi connect");
  delay(5000);

end:
  switch_LED(LED_OFF);
  ESP_LOGI(TAG, "Rebooting to %s firmware", (ret == 0 || ret == 2) ? "new" : "current");
  ota_display(5, "**", ""); // mark line rebooting
  delay(5000);
  // reboot: if ret was 0 switch back to normal mode
  // if ret was 2, stay in update mode
  do_reset(ret == 2);

} // start_ota_update

void ota_display(const uint8_t row, const std::string status,
                 const std::string msg) {
#ifdef HAS_DISPLAY
  dp_printf(112, row, 0, 0, status.substr(0, 2).c_str());
  if (!msg.empty()) {
    dp_printf(0, 7, 0, 0, "                ");
    dp_printf(0, 7, 0, 0, msg.substr(0, 16).c_str());
  }
  dp_dump(displaybuf);
#endif
}

// callback function to show download progress while streaming data
void show_progress(unsigned long current, unsigned long size) {
#ifdef HAS_DISPLAY
  char buf[17];
  snprintf(buf, 17, "%-9lu (%3lu%%)", current, current * 100 / size);
  ota_display(4, "**", buf);
#endif
}

#endif // USE_OTA
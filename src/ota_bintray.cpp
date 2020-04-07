#if (USE_OTA_BINTRAY)

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

const BintrayClient bintray(BINTRAY_USER, BINTRAY_REPO, BINTRAY_PACKAGE);

// Connection port (HTTPS)
const int port = 443;

// Variables to validate firmware content
int volatile contentLength = 0;
bool volatile isValidContentType = false;

// Local logging tag
static const char TAG[] = __FILE__;

// helper function to extract header value from header
inline String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}

// Reads data vom wifi client and flashes it to ota partition
// returns: 0 = finished, 1 = retry, -1 = abort
int do_ota_update() {

  char buf[17];
  bool redirect = true;
  size_t written = 0;

  // Fetch the latest firmware version
  ESP_LOGI(TAG, "Checking latest firmware version on server");
  ota_display(2, "**", "checking version");

  if (WiFi.status() != WL_CONNECTED)
    return 1;

  const String latest = bintray.getLatestVersion();

  if (latest.length() == 0) {
    ESP_LOGI(TAG, "Could not fetch info on latest firmware");
    ota_display(2, " E", "file not found");
    return -1;
  } else if (version_compare(latest, cfg.version) <= 0) {
    ESP_LOGI(TAG, "Current firmware is up to date");
    ota_display(2, "NO", "no update found");
    return -1;
  }
  ESP_LOGI(TAG, "New firmware version v%s available", latest.c_str());
  ota_display(2, "OK", latest.c_str());

  ota_display(3, "**", "");
  if (WiFi.status() != WL_CONNECTED)
    return 1;
  String firmwarePath = bintray.getBinaryPath(latest);
  if (!firmwarePath.endsWith(".bin")) {
    ESP_LOGI(TAG, "Unsupported binary format");
    ota_display(3, " E", "file type error");
    return -1;
  }

  String currentHost = bintray.getStorageHost();
  String prevHost = currentHost;

  WiFiClientSecure client;

  client.setCACert(bintray.getCertificate(currentHost));
  client.setTimeout(RESPONSE_TIMEOUT_MS);

  if (!client.connect(currentHost.c_str(), port)) {
    ESP_LOGI(TAG, "Cannot connect to %s", currentHost.c_str());
    ota_display(3, " E", "connection lost");
    goto abort;
  }

  while (redirect) {
    if (currentHost != prevHost) {
      client.stop();
      client.setCACert(bintray.getCertificate(currentHost));
      if (!client.connect(currentHost.c_str(), port)) {
        ESP_LOGI(TAG, "Redirect detected, but cannot connect to %s",
                 currentHost.c_str());
        ota_display(3, " E", "server error");
        goto abort;
      }
    }

    ESP_LOGI(TAG, "Requesting %s", firmwarePath.c_str());

    client.print(String("GET ") + firmwarePath + " HTTP/1.1\r\n");
    client.print(String("Host: ") + currentHost + "\r\n");
    client.print("Cache-Control: no-cache\r\n");
    client.print("Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if ((millis() - timeout) > (RESPONSE_TIMEOUT_MS)) {
        ESP_LOGI(TAG, "Client timeout");
        ota_display(3, " E", "client timeout");
        goto abort;
      }
    }

    while (client.available()) {
      String line = client.readStringUntil('\n');
      // Check if the line is end of headers by removing space symbol
      line.trim();
      // if the the line is empty, this is the end of the headers
      if (!line.length()) {
        break; // proceed to OTA update
      }

      // Check allowed HTTP responses
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") > 0) {
          ESP_LOGI(TAG, "Got 200 status code from server. Proceeding to "
                        "firmware flashing");
          redirect = false;
        } else if (line.indexOf("302") > 0) {
          ESP_LOGI(TAG, "Got 302 status code from server. Redirecting to "
                        "new address");
          redirect = true;
        } else {
          ESP_LOGI(TAG, "Could not get firmware download URL");
          goto retry;
        }
      }

      // Extracting new redirect location
      if (line.startsWith("Location: ")) {
        String newUrl = getHeaderValue(line, "Location: ");
        ESP_LOGI(TAG, "Got new url: %s", newUrl.c_str());
        newUrl.remove(0, newUrl.indexOf("//") + 2);
        currentHost = newUrl.substring(0, newUrl.indexOf('/'));
        newUrl.remove(newUrl.indexOf(currentHost), currentHost.length());
        firmwarePath = newUrl;
        continue;
      }

      // Checking headers
      if (line.startsWith("Content-Length: ")) {
        contentLength =
            atoi((getHeaderValue(line, "Content-Length: ")).c_str());
        ESP_LOGI(TAG, "Got %d bytes from server", contentLength);
      }

      if (line.startsWith("Content-Type: ")) {
        String contentType = getHeaderValue(line, "Content-Type: ");
        ESP_LOGI(TAG, "Got %s payload", contentType.c_str());
        if (contentType == "application/octet-stream") {
          isValidContentType = true;
        }
      }
    } // while (client.available())
  }   // while (redirect)

  ota_display(3, "OK", ""); // line download

  // check whether we have everything for OTA update
  if (!(contentLength && isValidContentType)) {
    ESP_LOGI(TAG, "Invalid OTA server response");
    ota_display(4, " E", "response error");
    goto retry;
  }

#if (HAS_LED != NOT_A_PIN)
#ifndef LED_ACTIVE_LOW
  if (!Update.begin(contentLength, U_FLASH, HAS_LED, HIGH)) {
#else
  if (!Update.begin(contentLength, U_FLASH, HAS_LED, LOW)) {
#endif
#else
  if (!Update.begin(contentLength)) {
#endif
    ESP_LOGI(TAG, "Not enough space to start OTA update");
    ota_display(4, " E", "disk full");
    goto abort;
  }

#ifdef HAS_DISPLAY
  // register callback function for showing progress while streaming data
  Update.onProgress(&show_progress);
#endif

  ota_display(4, "**", "writing...");
  written = Update.writeStream(client); // this is a blocking call

  if (written == contentLength) {
    ESP_LOGI(TAG, "Written %u bytes successfully", written);
    snprintf(buf, 17, "%ukB Done!", (uint16_t)(written / 1024));
    ota_display(4, "OK", buf);
  } else {
    ESP_LOGI(TAG, "Written only %u of %u bytes, OTA update attempt cancelled",
             written, contentLength);
  }

  if (Update.end()) {
    goto finished;
  } else {
    ESP_LOGI(TAG, "An error occurred. Error#: %d", Update.getError());
    snprintf(buf, 17, "Error#: %d", Update.getError());
    ota_display(4, " E", buf);
    goto retry;
  }

finished:
  client.stop();
  ESP_LOGI(TAG, "OTA update finished");
  return 0;

abort:
  client.stop();
  ESP_LOGI(TAG, "OTA update failed");
  return -1;

retry:
  return 1;

} // do_ota_update

// helper function to convert strings into lower case
bool comp(char s1, char s2) { return tolower(s1) < tolower(s2); }

// helper function to lexicographically compare two versions. Returns 1 if v2 is
// smaller, -1 if v1 is smaller, 0 if equal
int version_compare(const String v1, const String v2) {

  if (v1 == v2)
    return 0;

  const char *a1 = v1.c_str(), *a2 = v2.c_str();

  if (lexicographical_compare(a1, a1 + strlen(a1), a2, a2 + strlen(a2), comp))
    return -1;
  else
    return 1;
}

#endif // USE_OTA_BINTRAY
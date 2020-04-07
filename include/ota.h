#ifndef OTA_H
#define OTA_H

#if (USE_OTA)

#include "globals.h"
#include <ss_oled.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <string>
#include <algorithm>

// returns:
//   0 = finished -> reboot into new firmware
//   1 = retry -> call do_ota_update again
//   2 = finished-continue -> reboot into new firmware, stay in update mode
//  -1 = abort -> reboot into current firmware
int do_ota_update();
void start_ota_update();
void ota_display(const uint8_t row, const std::string status,
                 const std::string msg);
void show_progress(unsigned long current, unsigned long size);

#if (USE_OTA_BINTRAY)
int version_compare(const String v1, const String v2);
#include <BintrayClient.h>
#endif // USE_OTA_BINTRAY

#endif // USE_OTA

#endif // OTA_H

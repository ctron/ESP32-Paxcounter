#if (USE_OTA_HAWKBIT)

#include "ota.h"
#include <hawkbit.h>
#include <StreamString.h>

// this is the root ot Let's Encrypt
// you might need to change this to match your own root CA
const char * root_ca = "-----BEGIN CERTIFICATE-----\n\
MIIDSjCCAjKgAwIBAgIQRK+wgNajJ7qJMDmGLvhAazANBgkqhkiG9w0BAQUFADA/\n\
MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n\
DkRTVCBSb290IENBIFgzMB4XDTAwMDkzMDIxMTIxOVoXDTIxMDkzMDE0MDExNVow\n\
PzEkMCIGA1UEChMbRGlnaXRhbCBTaWduYXR1cmUgVHJ1c3QgQ28uMRcwFQYDVQQD\n\
Ew5EU1QgUm9vdCBDQSBYMzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\n\
AN+v6ZdQCINXtMxiZfaQguzH0yxrMMpb7NnDfcdAwRgUi+DoM3ZJKuM/IUmTrE4O\n\
rz5Iy2Xu/NMhD2XSKtkyj4zl93ewEnu1lcCJo6m67XMuegwGMoOifooUMM0RoOEq\n\
OLl5CjH9UL2AZd+3UWODyOKIYepLYYHsUmu5ouJLGiifSKOeDNoJjj4XLh7dIN9b\n\
xiqKqy69cK3FCxolkHRyxXtqqzTWMIn/5WgTe1QLyNau7Fqckh49ZLOMxt+/yUFw\n\
7BZy1SbsOFU5Q9D8/RhcQPGX69Wam40dutolucbY38EVAjqr2m7xPi71XAicPNaD\n\
aeQQmxkqtilX4+U9m5/wAl0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNV\n\
HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFMSnsaR7LHH62+FLkHX/xBVghYkQMA0GCSqG\n\
SIb3DQEBBQUAA4IBAQCjGiybFwBcqR7uKGY3Or+Dxz9LwwmglSBd49lZRNI+DT69\n\
ikugdB/OEIKcdBodfpga3csTS7MgROSR6cz8faXbauX+5v3gTt23ADq1cEmv8uXr\n\
AvHRAosZy5Q6XkjEGB5YGV8eAlrwDPGxrancWYaLbumR9YbK+rlmM6pZW87ipxZz\n\
R8srzJmwN0jP41ZL9c8PDHIyh8bwRLtTcm1D9SZImlJnt1ir/md2cXjbDaJWFBM5\n\
JDGFoqgCWjBH4d1QB7wCCZAA62RjYJsWvIjJEubSfZGL+T0yjWW06XyxV3bqxbYo\n\
Ob8VZRzI9neWagqNdwvYkQsEjgfbKbYK7p2CNTUQ\n\
-----END CERTIFICATE-----\n\
";

// Local logging tag
static const char TAG[] = __FILE__;

// the maximum number of operations we handle in a single update run
#define MAX_UPDATE_OPERATIONS 5

using namespace std;

void processUpdate(HawkbitClient& update, const Deployment& deployment);

int do_ota_update() {

  ESP_LOGI(TAG, "Begin Hawkit update");

  DynamicJsonDocument doc(16*1024);
  WiFiClientSecure client;
  client.setCACert(root_ca);
  HawkbitClient update(doc, client, HAWKBIT_URL, HAWKBIT_TENANT, HAWKBIT_DEVICE_ID, HAWKBIT_DEVICE_TOKEN);

  try {

    for(int i = 0; i < MAX_UPDATE_OPERATIONS; i++)
    {

      ESP_LOGI(TAG, "Fetch current state");
      State current = update.readState();
      ESP_LOGI(TAG, "State read");

      StreamString s;
      current.dump(s);
      ESP_LOGI(TAG, "State read: %s", s.c_str());

      switch (current.type())
      {

        case State::NONE: {
          ESP_LOGI(TAG, "No update pending");
          return 0;
        }

        case State::REGISTER: {
          EspClass esp;
          ESP_LOGI(TAG, "Need to register");
          update.updateRegistration(current.registration(),
              {
                {"mac", WiFi.macAddress()},
                {"app.version", PROGVERSION},
#ifdef HAS_RGB_LED
                {"app.led", "true"},
#endif
#if (HAS_DISPLAY)
                {"app.display", "true"},
#endif
#if (HAS_LORA)
                {"app.lora", "true"},
#endif
#if (HAS_GPS)
                {"app.gps", "true"},
#endif
                {"esp", "esp32"},
                {"esp32.chipRevision", String(esp.getChipRevision())},
                {"esp32.sdkVersion", esp.getSdkVersion()}
              });
          break;
        }
      
        case State::UPDATE: {
          const Deployment &deployment = current.deployment();
          update.reportProgress(deployment, 1, 2);
          try {
            processUpdate(update, deployment);
          } catch (const String &error) {
            ESP_LOGW(TAG, "Failed to perform update: %s", error.c_str());
            update.reportComplete(deployment, false, {error});
            return -1; // abort
          }

          // we do not re-try here, as we need to boot into the new version
          return 2;
        }

        case State::CANCEL: {
          ESP_LOGW(TAG, "Update cancel requested");
          update.reportCancelAccepted(current.stop());
          break;
        }

      } // end-switch
    } // end-while
  } // end-try
  catch (int err) {
    ESP_LOGE(TAG, "Failed to fetch update information: %d", err);
    return 1; // re-try
  } // end-catch

  // after "max" operations, we still have work to do, but we re-schedule
  return 0;

} // do_ota_update

void processUpdate(HawkbitClient& update, const Deployment& deployment) {

  if (deployment.chunks().size() != 1) {
    throw String("Expect update to have one chunk");
  }

  const Chunk& chunk = deployment.chunks().front();
  if (chunk.artifacts().size() != 1 ) {
    throw String("Expect update to have one artifact");
  }

  const Artifact& artifact = chunk.artifacts().front();

  ota_display(2, "OK", chunk.version().c_str());

  try {

    update.download(artifact, [artifact, deployment](Download& d){

      char buf[17];

      ota_display(3, "OK", "");

      // begin update

      int contentLength = artifact.size();

#if (HAS_LED != NOT_A_PIN)
#ifndef LED_ACTIVE_LOW
      if (!Update.begin(contentLength, U_FLASH, HAS_LED, HIGH)) {
#else
      if (!Update.begin(contentLength, U_FLASH, HAS_LED, LOW)) {
#endif
#else
      if (!Update.begin(contentLength)) {
#endif
        ota_display(4, " E", "disk full");
        if(Update.hasError()) {
          throw String(Update.errorString());
        } else {
          throw String("Failed to start update");
        }
      }

      // fetch checksum
      auto md5 = artifact.hashes().find("md5");

      // we have a checksum
      if(md5 != artifact.hashes().end()) {
        ESP_LOGI(TAG, "Use MD5 checksum: %s", md5->second.c_str());
        Update.setMD5(md5->second.c_str());
      }

#ifdef HAS_DISPLAY
      // register callback function for showing progress while streaming data
      Update.onProgress(&show_progress);
#endif

      // write update
      ota_display(4, "**", "writing...");
      int written = Update.writeStream(d.stream());

      if(written == contentLength) {
        snprintf(buf, 17, "%ukB Done!", (uint16_t)(written / 1024));
        ota_display(4, "OK", buf);
      }

      if(!Update.end()) {
        ESP_LOGI(TAG, "An error occurred. Error#: %d", Update.getError());
        snprintf(buf, 17, "Error#: %d", Update.getError());
        ota_display(4, " E", buf);
        if(Update.hasError()) {
          throw String(Update.errorString());
        } else {
          throw String("Failed to end update");
        }
      }
    }, "download-http");

  }
  catch ( DownloadError err ) {
    // download failed, we can re-try
    ESP_LOGW(TAG, "Failed to download new firmware: %d", err.code());
    return;
  }

  // all done

  update.reportComplete(deployment, true);

}

#endif // USE_OTA_HAWKBIT
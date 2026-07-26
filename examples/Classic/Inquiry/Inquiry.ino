#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  const EspBluedroidCapabilities capabilities = bluetooth.capabilities();
  if (!capabilities.classicInquiry)
  {
    Serial.println("Classic Inquiry is unavailable on this target");
    return;
  }
  if (!bluetooth.begin())
  {
    Serial.printf("begin failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.classic().inquiry().onResult(
    [](const EspBluedroidClassicInquiryResult &result) {
      Serial.printf("%s", result.address.c_str());
      if (!result.name.isEmpty())
      {
        Serial.printf(" name=%s", result.name.c_str());
      }
      if (result.hasRssi)
      {
        Serial.printf(" RSSI=%d", result.rssi);
      }
      if (result.hasClassOfDevice)
      {
        Serial.printf(" CoD=0x%06x",
          static_cast<unsigned>(result.classOfDevice));
      }
      Serial.println();
    });
  bluetooth.classic().inquiry().onComplete(
    [](const EspBluedroidClassicInquiryComplete &event) {
      Serial.printf("Inquiry complete (cancelled=%u)\n",
        event.cancelled ? 1 : 0);
    });

  EspBluedroidClassicInquiryConfig config;
  config.durationSeconds = 10;
  if (!bluetooth.classic().inquiry().start(config))
  {
    Serial.printf("Inquiry failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
  }
}

void loop()
{
  bluetooth.update();
  delay(1);
}

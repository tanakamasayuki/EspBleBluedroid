// en: CompileSmoke verifies that the library header builds and links on an
//     original ESP32 with the Bluedroid backend. It does not initialize the
//     Bluetooth stack.
// ja: CompileSmokeは無印ESP32のBluedroid構成でライブラリheaderがbuild/link
//     できることを確認する。Bluetooth stackは初期化しない。
#include <EspBleBluedroid.h>

void setup()
{
  Serial.begin(115200);
  Serial.printf("EspBleBluedroid %s\n", ESPBLEBLUEDROID_VERSION_STR);
}

void loop()
{
  delay(1000);
}


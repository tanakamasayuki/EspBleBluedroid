// en: HfpAudioGateway - play the phone's role in HFP. Starting the role makes
//     Classic connectable and discoverable, so no connect() call is needed: a
//     headset finds this board and connects. onPcmData() is the headset's
//     microphone, onPcmRequested() is the downlink call audio.
// ja: HfpAudioGateway - HFPでスマートフォン側のroleを演じる。roleを開始すると
//     Classicがconnectable・discoverableになるため connect() は不要で、ヘッドセット側
//     から見つけて接続してくる。onPcmData() はヘッドセットのマイク、
//     onPcmRequested() は下り通話音声。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Audio Gateway";
  if (!bluetooth.begin(config)) return;

  auto &gateway = bluetooth.classic().hfpAudioGateway();
  gateway.onConnected([](const EspBluedroidHfpSession &session) {
    Serial.printf("HF connected: %s\n", session.peerAddress.c_str());
  });
  gateway.onPcmData([](const EspBluedroidHfpPcmData &pcm) {
    // en: Microphone PCM received from the Hands-Free device: copy it out of
    //     the stack task promptly and do not retain the pointer.
    // ja: Hands-Free機器から届いたマイクのPCM。stack taskから速やかにコピーし、
    //     ポインタは保持しない。
    (void)pcm;
  });
  gateway.onPcmRequested([](EspBluedroidHfpPcmRequest &request) {
    // en: Replace silence with downlink call audio, and set request.written.
    // ja: 無音を下り通話音声へ置き換え、request.written を設定する。
    memset(request.data, 0, request.capacity);
    request.written = request.capacity;
  });
  gateway.start();
}

void loop()
{
  bluetooth.update();
  delay(1);
}

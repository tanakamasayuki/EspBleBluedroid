// en: A2dpSource - send audio to a Bluetooth speaker or headset. Type the sink's
//     Classic address, then PCM is pulled from onPcmRequested() after
//     startStream(). An AVRCP Target receives the speaker's transport and volume
//     commands. This example supplies silence; drop in a real source.
// ja: A2dpSource - Bluetoothスピーカーやヘッドセットへ音声を送る。Sinkの
//     Classic addressを入力すると、startStream() 後に onPcmRequested() からPCMが
//     引かれる。AVRCP Targetがスピーカー側の再生・音量操作を受け取る。この例は無音を
//     供給するので、実際の音源はここへ差し込む。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Audio Source";
  if (!bluetooth.begin(config))
  {
    Serial.printf("Bluetooth error: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &source = bluetooth.classic().a2dpSource();
  auto &target = bluetooth.classic().avrcpTarget();
  target.onCommand([](const EspBluedroidAvrcpCommandEvent &event) {
    Serial.printf("AVRCP command: command=%u state=%u\n",
      static_cast<unsigned>(event.command), static_cast<unsigned>(event.state));
  });
  target.onAbsoluteVolumeRequested([](const EspBluedroidAvrcpVolumeEvent &event) {
    Serial.printf("AVRCP volume: %u\n", event.volume);
  });
  if (!target.start())
    Serial.printf("AVRCP error: %s\n", bluetooth.lastErrorDetail().c_str());
  source.onPcmRequested([](EspBluedroidA2dpPcmRequest &request) {
    // en: flush means discard buffered audio (after a seek, for example):
    //     clear the queue and resampler state and return without writing.
    // ja: flushはバッファ済み音声の破棄（シーク後など）。queueとリサンプラの状態を
    //     クリアし、書き込まずに戻る。
    if (request.flush) return;
    // en: Replace this silence with PCM read from a bounded audio queue, and
    //     always set request.written - leaving it 0 sends nothing.
    // ja: この無音を bounded audio queue から読んだPCMへ置き換える。
    //     request.written は必ず設定する。0のままでは何も送られない。
    memset(request.data, 0, request.capacity);
    request.written = request.capacity;
  });
  source.onConnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("Connected: %s, session=%u\n",
      session.peerAddress.c_str(), static_cast<unsigned>(session.id));
    bluetooth.classic().a2dpSource().startStream();
  });
  source.onDisconnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("Disconnected: session=%u\n",
      static_cast<unsigned>(session.id));
  });

  if (!source.start())
    Serial.printf("A2DP error: %s\n", bluetooth.lastErrorDetail().c_str());
}

void loop()
{
  bluetooth.update();

  if (Serial.available())
  {
    const String address = Serial.readStringUntil('\n');
    if (!bluetooth.classic().a2dpSource().connect(address.c_str()))
      Serial.printf("Connect error: %s\n", bluetooth.lastErrorDetail().c_str());
  }
  delay(1);
}

// en: A2dpSink - receive music from a phone or PC and get the SBC-decoded audio as
//     16-bit interleaved PCM. Also starts an AVRCP Controller for transport
//     commands. Only one A2DP role may be active, so Sink and Source cannot run
//     together.
// ja: A2dpSink - スマートフォンやPCから音楽を受け取り、SBCをデコードした音声を
//     16-bit interleaved PCMとして受け取る。再生操作用にAVRCP Controllerも開始する。
//     A2DP roleは同時に1つだけなので、SinkとSourceは併用できない。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Audio Sink";
  if (!bluetooth.begin(config))
  {
    Serial.printf("Bluetooth error: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &sink = bluetooth.classic().a2dpSink();
  auto &controller = bluetooth.classic().avrcpController();
  controller.onConnected([](const EspBluedroidAvrcpConnection &) {
    Serial.println("AVRCP Controller connected");
  });
  controller.onCommandResponse([](const EspBluedroidAvrcpCommandEvent &event) {
    Serial.printf("AVRCP response: command=%u state=%u accepted=%u\n",
      static_cast<unsigned>(event.command), static_cast<unsigned>(event.state),
      event.accepted ? 1 : 0);
  });
  if (!controller.start())
    Serial.printf("AVRCP error: %s\n", bluetooth.lastErrorDetail().c_str());
  sink.onConnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("Connected: %s, session=%u\n",
      session.peerAddress.c_str(), static_cast<unsigned>(session.id));
  });
  sink.onPcmData([](const EspBluedroidA2dpPcmData &pcm) {
    // en: This runs on the A2DP stack task. Copy pcm.data to a bounded audio
    //     queue here; the pointer becomes invalid when this callback returns.
    // ja: これはA2DP stack taskで走る。pcm.data はここでbounded audio queueへ
    //     コピーする。callbackから戻るとポインタは無効になる。
    (void)pcm;
  });
  sink.onDisconnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("Disconnected: session=%u\n",
      static_cast<unsigned>(session.id));
  });

  if (!sink.start())
    Serial.printf("A2DP error: %s\n", bluetooth.lastErrorDetail().c_str());
}

void loop()
{
  bluetooth.update();
  delay(1);
}

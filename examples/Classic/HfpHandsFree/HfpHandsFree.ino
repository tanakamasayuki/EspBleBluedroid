// en: HfpHandsFree - act as a headset. connect() establishes the SLC (the control
//     channel), and connectAudio() brings up SCO (the channel that actually
//     carries voice) - a connected SLC does not mean there is audio. Audio is
//     mono 16-bit PCM produced by the Core's built-in CVSD/mSBC codec.
// ja: HfpHandsFree - ヘッドセットとして動作する。connect() はSLC（制御チャネル）を
//     確立し、connectAudio() がSCO（実際に音声を運ぶチャネル）を確立する。SLCが
//     繋がっていても音声があるとは限らない。音声はCore内蔵のCVSD/mSBC codecが作る
//     mono 16-bit PCM。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBluedroidHfpSessionId sessionId = 0;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Hands-Free";
  if (!bluetooth.begin(config)) return;

  auto &handsFree = bluetooth.classic().hfpHandsFree();
  handsFree.onConnected([](const EspBluedroidHfpSession &session) {
    sessionId = session.id;
    bluetooth.classic().hfpHandsFree().connectAudio(session.id);
  });
  handsFree.onAudioChanged([](const EspBluedroidHfpAudioChanged &event) {
    Serial.printf("audio=%u rate=%u\n", event.connected,
      static_cast<unsigned>(event.format.sampleRate));
  });
  handsFree.onPcmData([](const EspBluedroidHfpPcmData &pcm) {
    // en: Copy pcm.data to a bounded speaker queue before returning; it runs
    //     on the HFP stack task and the pointer dies with the callback.
    // ja: 戻る前に pcm.data をbounded speaker queueへコピーする。HFP stack taskで
    //     走り、ポインタはcallbackの終了で無効になる。
    (void)pcm;
  });
  handsFree.onPcmRequested([](EspBluedroidHfpPcmRequest &request) {
    // en: Replace silence with microphone PCM from a bounded queue.
    // ja: 無音を、bounded queueから読んだマイクのPCMへ置き換える。
    memset(request.data, 0, request.capacity);
    request.written = request.capacity;
  });
  if (!handsFree.start())
    Serial.printf("HFP error: %s\n", bluetooth.lastErrorDetail().c_str());
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const String address = Serial.readStringUntil('\n');
    if (!bluetooth.classic().hfpHandsFree().connect(address.c_str()))
      Serial.printf("Connect error: %s\n", bluetooth.lastErrorDetail().c_str());
  }
  delay(1);
}

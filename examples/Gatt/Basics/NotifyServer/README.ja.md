# Notify Server

購読中のClientへ1秒ごとにカウンタをNotificationで送ります。購読状態は `onSubscriptionChanged()`、送信は登録時に得たCharacteristic handleを渡す `notify()` で扱います。

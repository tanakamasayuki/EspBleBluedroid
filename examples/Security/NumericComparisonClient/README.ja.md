# Numeric Comparison Security Client

> English: [README.md](README.md)

DisplayYesNo capabilityでLE Secure Connections Numeric Comparisonを行います。この
clientとpeerへ表示された6桁値が一致することを確認し、`y`で承認、`n`で拒否します。

`onNumericComparison()`は`update()`から呼ばれます。Bluedroid側の確認処理は
`confirmNumericComparison()`を最大30秒待ち、`false`または時間切れならpairingを
拒否します。

Bluedroidは拒否後も暗号化されていないBLE linkを維持します。linkを閉じる場合は
applicationから`disconnect()`を呼びます。

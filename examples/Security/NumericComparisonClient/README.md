# Numeric Comparison Security Client

> 日本語版: [README.ja.md](README.ja.md)

Uses LE Secure Connections Numeric Comparison with DisplayYesNo capability.
Compare the six-digit value printed by this client with the value shown by the
peer, then enter `y` to accept or `n` to reject it.

`onNumericComparison()` runs from `update()`. The Bluedroid confirmation waits
for `confirmNumericComparison()` for up to 30 seconds and rejects pairing when
the application answers `false` or does not answer in time.

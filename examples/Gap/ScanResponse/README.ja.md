# ScanResponse

Advertising本体とScan Responseを別々に構成する例です。

- Advertising本体はpassive scanを含む全scannerへ届きます。
- Scan Responseはactive scanで要求したscannerだけへ届きます。
- それぞれ独立した31 byteの上限を持ちます。
- FlagsはAdvertising本体へ自動的に追加されます。

sketch内のコメントに各AD構造のbyte数と配置理由を記載しています。どちらかが
31 byteを超えると`start()`は`InvalidArgument`で失敗します。

Scan Responseが空なら、Advertising本体へ設定した名前は自動的にScan Responseへ
移されます。明示的に構成した場合は、必要な名前も明示してください。

# Classic Inquiry

> English: [README.md](README.md)

周囲のdiscoverableなBluetooth Classic機器をInquiryするexampleです。共有Bluedroid
stackの初期化前にcompile-time capability snapshotを確認し、`update()`から配送された
値型Inquiry Resultを表示します。

Classic InquiryとBLE Scanは意図的に分離しています。
`bluetooth.classic().inquiry()`はClassic address、name、Class of Device、RSSIを返し、
`bluetooth.scanner()`はBLE Advertising dataを返します。

resultと完了callbackを受け取るには`bluetooth.update()`を呼び続けてください。
`stop()`は停止を要求し、その後の完了eventで`cancelled=true`が通知されます。

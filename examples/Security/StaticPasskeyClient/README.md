# Static Passkey Security Client

> 日本語版: [README.ja.md](README.ja.md)

Uses a fixed six-digit passkey with MITM protection. This example is the
DisplayOnly side; enter the displayed value on a KeyboardOnly peer.

`onPasskeyDisplayed()` and `onSecurityChanged()` run from `update()`. Static
passkeys are convenient for embedded peers but must be treated as shared
secrets and changed from the example value for real products.

# PGP-Badge-Multi

## 日本語

これは[bentomo/PGP-Badge](https://github.com/bentomo/PGP-Badge)を基にした、**ESP32-WROOM-32用**の派生版です。元の1台接続版は`pgpemu-esp32`に残し、最大3台のPokémon GO端末に対応する版を`pgpemu-esp32-multi`に置いています。ESP32-C3用ではありません。

**現状:** 元の1台版はESP32-WROOM-32とiPhone版Pokémon GOで接続済み表示まで確認しています。複数端末版もESP-IDF 4.4.8でビルド・書き込み済みで、ログ上は3接続とも認証処理の終点まで進みました。青色GPIO2 LEDの接続台数表示は実機で確認済みです。3台のPokémon GO画面での同時接続、iPhoneとAndroidの混在、切断後の独立性は引き続き確認が必要です。

利用するためには、お手持ちの**初代Pokémon GO Plus**からBluetoothアドレス・Device Key・Blobを取得する必要があります。必要な機材とPC環境、取得時の注意、`secrets.c`への設定、ビルド、ESP32への書き込み、ログの見方は[日本語・英語併記の手順書](pgpemu-esp32-multi/README.md)をご覧ください。本リポジトリには実機用`secrets.c`も、それを組み込んだバイナリも含めていません。

元コードの[BSD 2-Clauseライセンス](LICENSE)と著作権表示は保持しています。[Yohanes Nugroho氏](https://github.com/yohanes)、[bentomo氏](https://github.com/bentomo/PGP-Badge)および元プロジェクトの貢献者の皆様に感謝します。[元プロジェクトのREADME](https://github.com/bentomo/PGP-Badge#readme)も参照できます。

3台で自動操作が止まり、再起動で復帰する症状の報告があります。通知処理の改善と、本体に診断ログを残して後からUSBで回収する機能を追加しています。対策の効果は実機検証中です。使い方は[手順書の診断ログの節](pgpemu-esp32-multi/README.md)をご覧ください。

接続別の状態管理、通知の再試行、保存ログの構造と制約は[設計書（日本語・英語）](pgpemu-esp32-multi/DESIGN.md)にまとめています。2026-10-10の修正版は、実機への書き込み・起動、保存ログ機能の有効化、Advertising開始まで確認済みです。3台使用時の停止症状が解消したという意味ではありません。

2026-10-11に[Watchdogと段階的な自動復旧](pgpemu-esp32-multi/README.md)を追加しました。端末別の認証・通知異常はその端末だけを切断し、処理タスクの停止などは本体再起動で復旧を試みます。通常の無通信では発動しません。実機での復旧効果は未検証です。

## English

This fork of [bentomo/PGP-Badge](https://github.com/bentomo/PGP-Badge) adds `pgpemu-esp32-multi` for up to three Pokémon GO BLE clients on **ESP32-WROOM-32**. The original single-client firmware remains in `pgpemu-esp32`. This is not an ESP32-C3 build.

**Status:** The original single-client version reached "connected" in Pokémon GO on an iPhone with ESP32-WROOM-32. The multi-client variant builds and flashes with ESP-IDF 4.4.8; logs show three connections progressing through authentication, and the blue GPIO2 connection-count indicator was observed. Simultaneous connected status in all three Pokémon GO apps, mixed iPhone/Android operation, and disconnect isolation still need confirmation.

All three clients have been reported to stop automatic actions until an ESP32 restart. Notification handling improvements and persistent diagnostics for later USB retrieval have been added; their effectiveness is undergoing field testing. See the diagnostics section of the [setup guide](pgpemu-esp32-multi/README.md).

Watchdog supervision and staged recovery were added on 2026-10-11: authentication/notification faults disconnect only the affected peer, while stalled execution can trigger an ESP32 restart. Ordinary inactivity is not a fault. Recovery effectiveness remains unverified on hardware; see the [setup guide](pgpemu-esp32-multi/README.md).

The [bilingual design document](pgpemu-esp32-multi/DESIGN.md) describes per-client state, notification retries, diagnostic storage, and limitations. On 2026-10-10, the revised firmware was successfully flashed and booted on hardware, enabled persistent diagnostics, and started advertising. This does not establish that the three-client stall has been resolved.

You need the Bluetooth address, Device Key, and Blob from an **original Pokémon GO Plus you own**. The [bilingual setup guide](pgpemu-esp32-multi/README.md) covers equipment, PC environment, extraction risks and steps, local `secrets.c`, building, flashing, and log checks. Neither a usable `secrets.c` nor a firmware binary containing it is published here.

The upstream [BSD 2-Clause license](LICENSE) and copyright notice are retained. Thanks to [Yohanes Nugroho](https://github.com/yohanes), [bentomo](https://github.com/bentomo/PGP-Badge), and the upstream contributors. The [original README](https://github.com/bentomo/PGP-Badge#readme) remains available upstream.

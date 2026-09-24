# PGP-Badge-Multi

## 日本語

これは[bentomo/PGP-Badge](https://github.com/bentomo/PGP-Badge)を基にした、**ESP32-WROOM-32用**の派生版です。元の1台接続版は`pgpemu-esp32`に残し、最大3台のPokémon GO端末に対応する版を`pgpemu-esp32-multi`に置いています。ESP32-C3用ではありません。

**現状:** 元の1台版はESP32-WROOM-32とiPhone版Pokémon GOで接続済み表示まで確認しています。複数端末版もESP-IDF 4.4.8でビルド・書き込み済みで、ログ上は3接続とも認証処理の終点まで進みました。青色GPIO2 LEDの接続台数表示は実機で確認済みです。3台のPokémon GO画面での同時接続、iPhoneとAndroidの混在、切断後の独立性は引き続き確認が必要です。

利用するためには、お手持ちの**初代Pokémon GO Plus**からBluetoothアドレス・Device Key・Blobを取得する必要があります。必要な機材とPC環境、取得時の注意、`secrets.c`への設定、ビルド、ESP32への書き込み、ログの見方は[日本語・英語併記の手順書](pgpemu-esp32-multi/README.md)をご覧ください。本リポジトリには実機用`secrets.c`も、それを組み込んだバイナリも含めていません。

元コードの[BSD 2-Clauseライセンス](LICENSE)と著作権表示は保持しています。[Yohanes Nugroho氏](https://github.com/yohanes)、[bentomo氏](https://github.com/bentomo/PGP-Badge)および元プロジェクトの貢献者の皆様に感謝します。[元プロジェクトのREADME](https://github.com/bentomo/PGP-Badge#readme)も参照できます。

## English

This fork of [bentomo/PGP-Badge](https://github.com/bentomo/PGP-Badge) adds `pgpemu-esp32-multi` for up to three Pokémon GO BLE clients on **ESP32-WROOM-32**. The original single-client firmware remains in `pgpemu-esp32`. This is not an ESP32-C3 build.

**Status:** The original single-client version reached "connected" in Pokémon GO on an iPhone with ESP32-WROOM-32. The multi-client variant builds and flashes with ESP-IDF 4.4.8; logs show three connections progressing through authentication, and the blue GPIO2 connection-count indicator was observed. Simultaneous connected status in all three Pokémon GO apps, mixed iPhone/Android operation, and disconnect isolation still need confirmation.

You need the Bluetooth address, Device Key, and Blob from an **original Pokémon GO Plus you own**. The [bilingual setup guide](pgpemu-esp32-multi/README.md) covers equipment, PC environment, extraction risks and steps, local `secrets.c`, building, flashing, and log checks. Neither a usable `secrets.c` nor a firmware binary containing it is published here.

The upstream [BSD 2-Clause license](LICENSE) and copyright notice are retained. Thanks to [Yohanes Nugroho](https://github.com/yohanes), [bentomo](https://github.com/bentomo/PGP-Badge), and the upstream contributors. The [original README](https://github.com/bentomo/PGP-Badge#readme) remains available upstream.

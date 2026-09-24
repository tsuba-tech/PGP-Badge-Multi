# PGP-Badge ESP32 multi-client firmware

[日本語はこちら](#日本語)

This directory adapts the original [PGP-Badge](https://github.com/bentomo/PGP-Badge) firmware for up to three simultaneous Pokémon GO BLE connections on an ESP32-WROOM-32. It uses ESP-IDF v4.4.8 and Bluedroid. The original project is distributed under the BSD 2-Clause license; retain the repository's [LICENSE](../LICENSE) and copyright notice when redistributing it.

The code has been built for ESP32. Simultaneous iOS and Android connection, authentication, and reconnection still require hardware testing.

## Acknowledgments

Thanks to [Yohanes Nugroho](https://github.com/yohanes) for the Pokémon GO Plus reverse-engineering work and original BSD-2-Clause-licensed code, and to [bentomo](https://github.com/bentomo/PGP-Badge) and the upstream contributors for the PGP-Badge hardware and ESP32 firmware. This variant builds on their work; it is not an official Pokémon GO or ESP-IDF project.

## Local device data

`main/secrets.c` contains device-specific MAC, DEVICE_KEY, and BLOB values. It is ignored by Git. Never commit it, a firmware binary built with it, or a build directory to a public repository. To prepare a local build, copy `main/secrets.example.c` to `main/secrets.c`, then replace the placeholder values and remove its `#error` line. Use values from a device you own.

If this repository already has a local `main/secrets.c`, keep it local and do not replace it with the example.

## Install and flash (Windows / PowerShell)

1. Prepare an ESP32-WROOM-32 development board, a data-capable USB cable, Git, and [ESP-IDF v4.4.8 with its tools](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/get-started/index.html). Select the **ESP32** target, not ESP32-C3. If the USB serial port does not appear, install the driver appropriate to your board's USB-to-serial chip.
2. Clone this repository and enter the multi-client project:

   ```powershell
   git clone https://github.com/tsuba-tech/PGP-Badge-Multi.git
   cd .\PGP-Badge-Multi\pgpemu-esp32-multi
   ```

3. If `main/secrets.c` does not already exist, copy the example. Edit the copy with your own device data and remove its `#error` line. **Do not overwrite an existing local `secrets.c`.**

   ```powershell
   if (!(Test-Path .\main\secrets.c)) { Copy-Item .\main\secrets.example.c .\main\secrets.c }
   ```

4. In the same PowerShell session, activate your ESP-IDF installation, select ESP32, and build:

   ```powershell
   & "C:\esp\v4.4.8\esp-idf\export.ps1" # adjust to your IDF installation
   idf.py set-target esp32
   idf.py build
   ```

   If PowerShell blocks `export.ps1`, run `Set-ExecutionPolicy -Scope Process Bypass` in that session and retry. If switching ESP-IDF versions or targets in an existing checkout, run `idf.py fullclean` before rebuilding. The project expects `CONFIG_BTDM_CTRL_BLE_MAX_CONN >= 3`; `sdkconfig.defaults` requests 3.
5. Connect the board by USB, identify its COM port in Windows Device Manager, then flash and open the serial monitor. Replace `COM3` with the port of **your** board:

   ```powershell
   idf.py -p COM3 flash monitor
   ```

   Exit the monitor with `Ctrl+]`. The locally generated firmware contains your device data; do not upload it to GitHub or attach it to a release. Flashing and BLE behavior have not yet been verified on the target board.

## GPIO2 status LED

The firmware drives the development board's single-color GPIO2 LED with an active-high status pattern:

| BLE connections | LED pattern |
| --- | --- |
| 0 | Solid on while advertising |
| 1 | One short flash, then one long flash, repeated |
| 2 | Two short flashes, then one long flash, repeated |
| 3 | Three short flashes, then one long flash, repeated |

On the original PGP-Badge schematic GPIO2 is unconnected. This indicator is for an ESP32 development board with a GPIO2 LED or an external LED wired to GPIO2. For an active-low LED, change `STATUS_LED_ACTIVE_LEVEL` in `main/pgpemu.c` from `1` to `0`. The development board's red LED indicates power and is not controlled by this firmware. Game-event indication is not implemented yet because the Pokémon versus Pokéstop LED command patterns have not been verified on the target devices.

## Hardware validation

After flashing, connect an iPhone first and confirm authentication and resumed advertising. Connect an Android device, confirm authentication on both, then connect a third device. Disconnect one, verify the others stay connected, then reconnect it. The serial log reports `conn_id`, client slot, active count, authentication transitions, advertising restarts, and prepare/execute writes. A successful build alone does not establish successful simultaneous connections.

---

# 日本語

## PGP-Badge ESP32 複数端末対応版

このディレクトリは、元の [PGP-Badge](https://github.com/bentomo/PGP-Badge) のファームウェアを、ESP32-WROOM-32で最大3台のPokémon GO端末と同時にBLE接続できるよう改修したものです。ESP-IDF v4.4.8とBluedroidを使用します。元プロジェクトはBSD 2-Clauseライセンスです。再配布時はリポジトリの[LICENSE](../LICENSE)と著作権表示を残してください。

ESP32向けのビルドは成功しています。ただし、iPhoneとAndroidを含む複数端末での同時接続・認証・再接続は、これから実機で検証します。

## 謝辞

Pokémon GO PlusのリバースエンジニアリングとBSD 2-Clauseライセンスの元コードを公開された[Yohanes Nugroho氏](https://github.com/yohanes)、PGP-BadgeのハードウェアとESP32ファームウェアを公開された[bentomo氏](https://github.com/bentomo/PGP-Badge)、および元リポジトリの貢献者の皆様に感謝します。この派生版は皆様の成果を基にしています。Pokémon GOやESP-IDFの公式プロジェクトではありません。

## 実機用データと公開用ファイル

`main/secrets.c`には実機固有のMAC、DEVICE_KEY、BLOBを入れます。このファイルはGitの管理対象外です。実機用の`secrets.c`、それを組み込んだファームウェアのバイナリ、`build`ディレクトリを公開リポジトリへ追加しないでください。

新たにローカルビルドを準備するときは、`main/secrets.example.c`を`main/secrets.c`へコピーし、所有する機器の値で仮の値を置き換えて、`#error`行を削除してください。すでにローカルに実機用`main/secrets.c`がある場合は、サンプルで上書きしないでください。

## インストール・書き込み（Windows / PowerShell）

1. ESP32-WROOM-32開発ボード、データ通信できるUSBケーブル、Git、[ESP-IDF v4.4.8と必要なツール](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/get-started/index.html)を用意します。ターゲットは**ESP32**で、ESP32-C3ではありません。USBシリアルポートが表示されない場合は、開発ボードのUSBシリアル変換チップに合うドライバーを導入してください。
2. リポジトリを取得して、複数端末対応版のディレクトリへ移動します。

   ```powershell
   git clone https://github.com/tsuba-tech/PGP-Badge-Multi.git
   cd .\PGP-Badge-Multi\pgpemu-esp32-multi
   ```

3. `main/secrets.c`がまだない場合だけサンプルをコピーします。コピーしたファイルに所有する機器の値を設定し、`#error`行を削除します。**すでにある実機用`secrets.c`を上書きしないでください。**

   ```powershell
   if (!(Test-Path .\main\secrets.c)) { Copy-Item .\main\secrets.example.c .\main\secrets.c }
   ```

4. 同じPowerShellセッションでESP-IDFを有効化し、ESP32を選んでビルドします。

   ```powershell
   & "C:\esp\v4.4.8\esp-idf\export.ps1" # インストール先が異なる場合は変更
   idf.py set-target esp32
   idf.py build
   ```

   `export.ps1`が実行ポリシーでブロックされる場合は、そのセッション内で`Set-ExecutionPolicy -Scope Process Bypass`を実行して再試行してください。既存の作業ディレクトリでESP-IDFの版やターゲットを切り替えた場合は、再ビルド前に`idf.py fullclean`を実行します。`CONFIG_BTDM_CTRL_BLE_MAX_CONN`は3以上が必要で、`sdkconfig.defaults`では3を指定しています。
5. 開発ボードをUSB接続し、WindowsのデバイスマネージャーでCOMポートを確認します。`COM3`を実際のポート名に置き換え、書き込みとシリアルログ表示を行います。

   ```powershell
   idf.py -p COM3 flash monitor
   ```

   モニターを終了するには`Ctrl+]`を押します。ローカルで生成したファームウェアには実機用データが含まれるため、GitHubやReleaseにアップロードしないでください。対象ボードへの書き込みとBLE動作は、まだ実機で確認していません。

## GPIO2の青色ステータスLED

開発ボードのGPIO2につながった単色LEDを、HIGHで点灯する設定で制御します。

| BLE接続台数 | LED表示 |
| --- | --- |
| 0台 | Advertising中は点灯 |
| 1台 | 短く1回、長く1回の点滅を繰り返す |
| 2台 | 短く2回、長く1回の点滅を繰り返す |
| 3台 | 短く3回、長く1回の点滅を繰り返す |

元のPGP-Badge基板の回路図ではGPIO2は未接続です。この表示はGPIO2にLEDを備えたESP32開発ボード、またはGPIO2へ外付けしたLED用です。LOWで点灯するLEDなら、`main/pgpemu.c`の`STATUS_LED_ACTIVE_LEVEL`を`1`から`0`へ変更してください。開発ボードの赤色LEDは電源表示であり、このファームウェアからは制御しません。ポケモン・ポケストップのイベント別表示は、対象機器でのLEDコマンドの違いを確認できていないため未実装です。

## 実機確認

書き込み後、まずiPhoneを接続し、認証成功とAdvertising再開を確認します。続いてAndroidを接続し、両方の認証と接続維持を確認してから、3台目を接続します。1台だけ切断して残りの接続が続くことを確認し、切断した端末を再接続してください。シリアルログでは`conn_id`、client slot、接続数、認証状態の変化、Advertising再開、prepare/execute writeを確認できます。

ビルド成功だけでは、3台の同時接続やiOS・Android間での状態混線がないことは証明できません。

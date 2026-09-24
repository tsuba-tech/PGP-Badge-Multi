# ESP32-WROOM-32版 PGP-Badge-Multi

[日本語](#日本語) | [English](#english)

## 日本語

### これは何？現在の状態は？

初代Pokémon GO Plusを模したBLEファームウェアです。元の1台版は`../pgpemu-esp32`に残し、このディレクトリで最大3台の接続を扱います。ESP32-C3用ではありません。GPIO2の青色LEDで接続台数とゲーム結果を表示します。

元の1台版はESP32-WROOM-32とiPhone版Pokémon GOで接続済み表示まで確認しています。**この複数端末版はESP-IDF 4.4.8でビルド成功。ただしiPhone・Android・3台目の同時接続とLED結果表示は、これから実機で検証します。** この段階では動作保証版として扱わないでください。

### 0. 準備するもの

| 種類 | 必要なもの |
| --- | --- |
| コピー元 | 自分が所有する**初代Pokémon GO Plus** 1台。Pokémon GO Plus +、Poké Ball Plus、Go-tcha用の手順ではありません。 |
| Secret取得 | Android端末、元機器の電池、[Suota Go+](https://github.com/Jesus805/Suota-Go-Plus)のAPKと`patch.img`。この旧ツールはアーカイブ済みで、現行Androidで動作する保証はありません。 |
| 書き込み先 | GPIO2に青色LEDを備えたESP32-WROOM-32開発ボード、データ通信対応USBケーブル。GPIO2 LEDがないボードでもBLE部分は使えます。 |
| ビルドPC | 64bit Windows、PowerShell、Git、[ESP-IDF **v4.4.8** とESP32用ツール](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/get-started/index.html)。ESP-IDFのWindowsインストーラーを使うと、Python、CMake、Ninja、コンパイラー、書き込みツールもセットアップできます。 |
| 動作確認 | Pokémon GOが使えるiPhoneとAndroid端末。3台同時試験には3台目のスマートフォンも必要です。 |

Python 2.7は不要です。元リポジトリにあるPython 2.7の説明は古い手順です。ESP-IDF 4.4.8のインストーラーが管理するPythonを使ってください。この開発環境でのビルドは**Python 3.11.9**で通っていますが、3.11.9を別途インストールする必要はありません。`idf.py`はESP-IDFに付属するコマンドで、設定・ビルド・書き込み・シリアルログ表示をまとめて実行します。

Gitが未導入ならPowerShellで`winget install --id Git.Git -e`を実行し、**PowerShellを開き直して**`git --version`で確認します。`winget`が使えなければ[Git for Windows](https://gitforwindows.org/)から導入してください。ESP-IDFも後述の公式Windowsインストーラーで先に導入します。PythonやCMakeを個別に集める必要はありません。

### 1. 所有するPokémon GO Plusから3つの値を取得する

このファームウェアには、元機器のBluetoothアドレス（6バイト）、Device Key（16バイト）、Blob（256バイト）が必要です。本リポジトリにはこれらを含めません。取得に使う[Suota Go+の公式手順](https://github.com/Jesus805/Suota-Go-Plus#running)は、**元機器に一時的なファームウェアを書き込む**方法です。失敗すると元機器が使用不能になる可能性があります。リスクを許容できない場合は、この工程を実行しないでください。

1. [Suota Go+の配布ページ](https://github.com/Jesus805/Suota-Go-Plus/releases)と元プロジェクトの説明を読み、APKと`patch.img`を用意します。非公式の再配布サイトからは取得しないでください。元機器の電池を新しくし、Androidの近くに置きます。
2. AndroidへAPKをインストールし、Suota Go+を起動します。アプリが作る`SuotaPgp`フォルダーへ`patch.img`を置きます。OSがインストールを許可しない、またはツールが起動しない場合は、無理に進めないでください。
3. 元のPokémon GO PlusをPokémon GOに接続します。Suota Go+の「Patch Device」で**Refresh → 接続中の元機器を選択 → `patch.img`を選択 → Start Patch**の順に操作します。
4. ツールが完了を示したら約60秒待ち、「Key Extractor」で**Scan → PGP Key Extractor → Get Device Info → Save**の順に操作します。`SuotaPgp`内にJSONが保存されます。
5. JSONに`bluetooth`、`device`、`blob`の3項目があることを確認します。順にBluetoothアドレス、Device Key、Blobです。16進数字の長さは区切り記号を除いて**12・32・512桁**です。JSONを安全な方法でPCへ移し、公開場所やチャットへ貼らないでください。
6. **元機器を必ず元の状態へ戻します。** 同じ画面の**Restore PGP**を押し、「Restore Complete」の表示と自動再起動を待ちます。元機器が通常のPokémon GO Plusとして再接続できることを確認してください。復元に失敗した場合は作業を止め、[元ツールの説明](https://github.com/Jesus805/Suota-Go-Plus)を参照してください。

上記は元ツールが公開した手順の案内です。このリポジトリは抽出ツールを同梱・保守していません。

### 2. リポジトリとローカル専用の`secrets.c`を準備する

PowerShellで次を実行します。すでにチェックアウトがある場合、`git clone`は不要です。

```powershell
git clone https://github.com/tsuba-tech/PGP-Badge-Multi.git
cd .\PGP-Badge-Multi\pgpemu-esp32-multi
```

Suota Go+のJSONをPCへ移したら、同梱の変換スクリプトで**ローカルにだけ**`main/secrets.c`を生成できます。`C:\path\device.json`は実際のJSONパスへ置き換えてください。

```powershell
.\tools\json-to-secrets.ps1 -InputJson "C:\path\device.json"
```

このスクリプトも実行ポリシーで止められた場合は、同じPowerShellで`Set-ExecutionPolicy -Scope Process Bypass`を実行してから再試行します。これはそのセッション限りの変更です。

スクリプトは3項目の16進数字を検証し、`MAC[6]`、`DEVICE_KEY[16]`、`BLOB[256]`のC配列へ変換します。既存の`main/secrets.c`は**上書きしません**。スクリプトを使えない場合は、`main/secrets.example.c`を`main/secrets.c`にコピーして編集します。JSONの`bluetooth`→`MAC`、`device`→`DEVICE_KEY`、`blob`→`BLOB`です。各16進2桁を`0xNN`とし、カンマで並べてください。**バイト順を逆にせず**、サンプルの`#error`行を削除します。実データをREADME、GitHub Issues、チャットに貼らないでください。

`main/secrets.c`と、これを組み込んだ`build`以下のバイナリはGitの公開対象から除外しています。公開用の`secrets.example.c`はダミー値だけで、そのままではビルドできません。

### 3. ESP-IDFを有効化してビルドする

[ESP-IDF 4.4.8のWindows向け導入ガイド](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/get-started/windows-setup.html)に従ってインストールします。下の`C:\esp\v4.4.8\esp-idf`はこの開発PCでの場所です。別のPCでは実際のインストール先に置き換えてください。PowerShellは上記のプロジェクトディレクトリで開きます。

```powershell
& "C:\esp\v4.4.8\esp-idf\export.ps1"
python --version
idf.py --version
idf.py set-target esp32
idf.py build
```

`export.ps1`はそのPowerShellセッションでESP-IDFのPython環境と各ツールを使えるようにします。実行ポリシーでブロックされた場合に限り、そのセッションだけ`Set-ExecutionPolicy -Scope Process Bypass`を実行し、`export.ps1`を再実行してください。`idf.py`が見つからない場合は`export.ps1`が成功したか確認します。`set-target esp32`はESP32-WROOM-32を選ぶ操作で、ESP32-C3を選ばないでください。

`CONFIG_BTDM_CTRL_BLE_MAX_CONN`は3以上が必要です。`sdkconfig.defaults`に3を指定しています。以前の設定を持ち込んだ場合は、ビルド前に生成された`sdkconfig`の値を確認してください。ESP-IDFの版やターゲットを切り替えた場合は`idf.py fullclean`後に再ビルドします。`build`内のバイナリにはSecretが埋め込まれるため、Releaseなどで配布しないでください。

### 4. ESP32へ書き込み、ログを見る

開発ボードをUSB接続し、Windowsの「デバイスマネージャー → ポート（COMとLPT）」でCOM番号を確認します。`COM3`は例です。

```powershell
idf.py -p COM3 flash monitor
```

`flash`がESP32へ書き込み、`monitor`が起動後のシリアルログを表示します。モニターを閉じるには`Ctrl+]`を押します。ポートが見つからなければUSBケーブルがデータ通信用か、ドライバーが入っているかを確認します。ポートが使用中なら他のシリアルモニターを閉じます。自動書き込みが始まらないボードでは、画面に`Connecting...`が出たときにBOOTボタン操作が必要な場合があります。

以前のWROOM-32実機試験では、初回に`Wrong boot mode detected (0x13)`が出て書き込みに失敗しました。これはビルドエラーではありません。再実行時に**BOOTを押したまま書き込み開始 → `Connecting...`が出たら離す**ことで書き込めました。成功時は`Hash of data verified`と`Hard resetting via RTS pin`が目安です。起動時にBOOTを押す必要はありません。

### 5. 青色LEDと接続ログを確認する

開発ボードの**青色GPIO2 LEDのみ**をソフトウェアで制御します。赤色LEDは電源表示で制御できません。HIGHで点灯するボード用です。LOWで点灯するボードなら`main/pgpemu.c`の`STATUS_LED_ACTIVE_LEVEL`を`0`にして再ビルドします。

| 状態 | 青色LED |
| --- | --- |
| 接続0台・Advertising中 | 点灯 |
| 接続1台 | 短く1回＋長く1回、繰り返し |
| 接続2台 | 短く2回＋長く1回、繰り返し |
| 接続3台 | 短く3回＋長く1回、繰り返し |
| ポケストップでアイテム取得のLED通知 | 短く4回、その後は接続台数表示へ戻る |
| ポケモン捕獲成功のLED通知 | 短く5回、その後は接続台数表示へ戻る |

4回・5回はPokémon GOが送る色の並びから結果を判別します。**単にポケストップやポケモンが現れた時ではなく、成功結果の通知が来た時**です。現行のiOS/Android版での分類は、実機試験で確認します。両方の結果通知が近接した場合は順番に表示します。

シリアルログでは次を順に見てください。`conn_id`は端末ごとの接続番号、`slot`はこのファームウェアの0〜2の保管場所です。数字は実際の接続で変わります。

```text
advertising start successfully
Client connected: conn_id=... slot=0
Active clients: 1/3
Advertising restarted for next client
conn_id=... cert_state=2 -> 6
Client connected: conn_id=... slot=1
Active clients: 2/3
Client connected: conn_id=... slot=2
Active clients: 3/3
```

`cert_state=... -> 6`はファームウェア側の認証手順が終点に進んだ目安です。**Pokémon GOの画面でも接続済みか確認**してください。Androidで長いGATT書き込みが起きると`prepare write conn_id=...`と`exec write conn_id=...`が表示されます。LED結果を認識すると`Game result conn_id=...: Pokestop items`または`Pokemon caught`が表示されます。

iPhoneを接続したままAndroid、その後3台目を接続し、全端末の接続表示とLEDの1→2→3台パターンを確認します。1台だけ切断したら、その`conn_id`の`Client disconnected`と接続数の減少、Advertising再開を確認します。残りの端末がPokémon GO上で接続済みのままか確かめ、切断した端末を再接続します。ログには機器アドレスや認証データが含まれ得るため、共有前に伏せてください。

### 謝辞・ライセンス

[Yohanes Nugroho氏](https://github.com/yohanes/pgpemu)の解析と元コード、[bentomo氏とPGP-Badgeの貢献者](https://github.com/bentomo/PGP-Badge)のハードウェア／ESP32実装、[Jesus Bamford氏のSuota Go+](https://github.com/Jesus805/Suota-Go-Plus)の公開情報に感謝します。LEDコマンドの解析には[Fortinetの一次解析記事](https://www.fortinet.com/blog/threat-research/pokemon-go-plus-preview-through-reverse-engineering)、結果パターンの判別には[ar5hil氏のpgpemu-1](https://github.com/ar5hil/pgpemu-1)も参考にしました。元コードの[BSD 2-Clauseライセンス](../LICENSE)と著作権表示を保持しています。Pokémon GOやESP-IDFの公式プロジェクトではありません。

---

## English

### Purpose and status

This ESP32-WROOM-32 firmware emulates an original Pokémon GO Plus for up to three BLE clients. The original single-client version remains in `../pgpemu-esp32` and previously reached "connected" in Pokémon GO on an iPhone. This is not for ESP32-C3. The multi-client firmware **builds under ESP-IDF 4.4.8**, but three-client iPhone/Android operation and GPIO2 game-result patterns still require hardware testing.

### 0. What you need

| Category | Requirement |
| --- | --- |
| Source device | An **original Pokémon GO Plus you own**. These steps are not for Pokémon GO Plus +, Poké Ball Plus, or Go-tcha. |
| Extraction | Android phone, a fresh battery, the archived [Suota Go+](https://github.com/Jesus805/Suota-Go-Plus) APK and `patch.img`. Compatibility with current Android versions is unverified. |
| Target | ESP32-WROOM-32 development board, data-capable USB cable, and preferably a GPIO2 blue LED. |
| Build PC | 64-bit Windows, PowerShell, Git, and [ESP-IDF **v4.4.8** plus ESP32 tools](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/get-started/index.html). Its Windows installer sets up Python, CMake, Ninja, compiler, and flashing tools. |
| Testing | iPhone and Android with Pokémon GO; a third phone for three-client testing. |

Python 2.7 in the historical upstream README is **not** needed. Use the Python environment installed by ESP-IDF. Our build succeeded with Python **3.11.9**, but you do not need to install that exact version separately. `idf.py` is ESP-IDF's command-line tool for selecting the target, building, flashing, and displaying serial logs.

If Git is missing, run `winget install --id Git.Git -e` in PowerShell, reopen PowerShell, and check `git --version`. If `winget` is unavailable, install [Git for Windows](https://gitforwindows.org/). Install ESP-IDF with the official Windows installer described below; you do not need to collect Python and CMake separately.

### 1. Extract the three values from your own device

The firmware needs the original device's Bluetooth address (6 bytes), Device Key (16 bytes), and Blob (256 bytes). [Suota Go+'s published procedure](https://github.com/Jesus805/Suota-Go-Plus#running) **temporarily modifies the original device's firmware**. A failure can render it unusable. Do not proceed if that risk is unacceptable.

1. Read the [Suota Go+ project](https://github.com/Jesus805/Suota-Go-Plus) and obtain its APK and `patch.img` from the [project's releases](https://github.com/Jesus805/Suota-Go-Plus/releases), not an unrelated mirror. Put a fresh battery in the original device and keep it near the Android phone.
2. Install and open the APK; put `patch.img` in the `SuotaPgp` folder the app creates. If installation or launch fails on your Android version, stop rather than forcing it.
3. Connect the original device to Pokémon GO. In Suota Go+'s **Patch Device** tab, use **Refresh → select the paired device → select `patch.img` → Start Patch**.
4. After completion, wait about 60 seconds. In **Key Extractor**, use **Scan → PGP Key Extractor → Get Device Info → Save**. A JSON file is saved in `SuotaPgp`.
5. Verify JSON fields `bluetooth`, `device`, and `blob`, containing respectively **12, 32, and 512 hex digits** after separators are removed. Transfer the JSON privately to the PC; do not post it in chat or an issue.
6. **Restore the original device:** tap **Restore PGP** in the device-information screen, wait for **Restore Complete** and the automatic restart, then confirm it reconnects as a normal Pokémon GO Plus. Stop and consult the [tool's documentation](https://github.com/Jesus805/Suota-Go-Plus) if restoration fails.

This repository does not distribute or maintain the extraction tool.

### 2. Clone and create local-only `secrets.c`

In PowerShell (skip `git clone` if you already have the checkout):

```powershell
git clone https://github.com/tsuba-tech/PGP-Badge-Multi.git
cd .\PGP-Badge-Multi\pgpemu-esp32-multi
.\tools\json-to-secrets.ps1 -InputJson "C:\path\device.json"
```

If PowerShell blocks this script, run `Set-ExecutionPolicy -Scope Process Bypass` in the same session and retry. This change lasts only for that session.

Replace the JSON path with your file. The helper validates all three values and generates local `main/secrets.c` with `MAC[6]`, `DEVICE_KEY[16]`, and `BLOB[256]`. It **refuses to overwrite** an existing file. For manual entry, copy `main/secrets.example.c` to `main/secrets.c`, map JSON `bluetooth`→`MAC`, `device`→`DEVICE_KEY`, `blob`→`BLOB`, write each byte as `0xNN` separated by commas **without reversing byte order**, and remove the example's `#error` line. Never share real values.

The real `secrets.c` and `build` output are Git-ignored. The published example contains only dummy data and cannot build as-is.

### 3. Activate ESP-IDF and build

Follow Espressif's [v4.4.8 Windows setup guide](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/get-started/windows-setup.html). Adjust the example installation path to your PC. Run in the multi-client project directory:

```powershell
& "C:\esp\v4.4.8\esp-idf\export.ps1"
python --version
idf.py --version
idf.py set-target esp32
idf.py build
```

`export.ps1` activates ESP-IDF tools in that PowerShell session. If execution policy blocks it, run `Set-ExecutionPolicy -Scope Process Bypass` for that session only and retry. If `idf.py` is not found, check that export completed. Use target **esp32**, not esp32c3. `sdkconfig.defaults` requests `CONFIG_BTDM_CTRL_BLE_MAX_CONN=3`; check generated `sdkconfig` if reusing old settings. After changing IDF version or target in an existing checkout, run `idf.py fullclean` before rebuilding. The built firmware embeds your device data: do not publish it as a release.

### 4. Flash and monitor

Connect the board by USB. Find its COM port in Windows Device Manager under **Ports (COM & LPT)**. Replace example `COM3` below:

```powershell
idf.py -p COM3 flash monitor
```

`flash` uploads to the ESP32; `monitor` displays serial logs. Exit monitor with `Ctrl+]`. If no port appears, check cable and USB-serial driver. If the port is busy, close other serial monitors. Some boards need the BOOT button when `Connecting...` appears. Flashing this multi-client variant has not yet been verified.

In a previous **single-client WROOM-32** trial, the first attempt failed with `Wrong boot mode detected (0x13)`; this was not a build error. Retrying while holding **BOOT**, then releasing it when `Connecting...` appeared, succeeded. `Hash of data verified` and `Hard resetting via RTS pin` indicate a successful flash. Normal boot does not require BOOT. Flashing this multi-client variant is still pending hardware verification.

### 5. LED and connection checks

Only the **blue GPIO2 LED** is software-controlled; the board's red LED is a power indicator. The default assumes HIGH turns blue on. For active-low boards, set `STATUS_LED_ACTIVE_LEVEL` in `main/pgpemu.c` to `0` and rebuild.

| State | Blue LED |
| --- | --- |
| 0 clients, advertising | Steady on |
| 1 / 2 / 3 clients | 1 / 2 / 3 short flashes, then one long flash, repeating |
| PokéStop item success LED command | 4 short flashes, then resume connection count |
| Pokémon capture success LED command | 5 short flashes, then resume connection count |

Four and five flashes mean a **result notification**, not merely a nearby Pokémon or PokéStop. Result classification from the app's RGB LED sequence still needs iOS/Android hardware validation. Near-simultaneous results are queued.

Look for `advertising start successfully`, then `Client connected: conn_id=... slot=0`, `Active clients: 1/3`, and `Advertising restarted for next client`. The firmware's authentication progression should reach `conn_id=... cert_state=2 -> 6` (or `5 -> 6` on reconnect), **and Pokémon GO must also show connected**. Android long writes may log `prepare write conn_id=...` and `exec write conn_id=...`. Recognized results log `Game result conn_id=...: Pokestop items` or `Pokemon caught`.

Keep the iPhone connected, add Android, then a third phone; expect distinct `conn_id` and slots 0, 1, and 2, with `Active clients: 3/3`. Disconnect only one, check the remaining phones stay connected, advertising resumes, and the disconnected phone can reconnect. Redact addresses and authentication traffic before sharing logs.

### Credits and license

Thanks to [Yohanes Nugroho](https://github.com/yohanes/pgpemu) for the original reverse engineering and code, [bentomo and PGP-Badge contributors](https://github.com/bentomo/PGP-Badge) for hardware and ESP32 firmware, and [Jesus Bamford](https://github.com/Jesus805/Suota-Go-Plus) for the extraction project. [Fortinet's LED-command research](https://www.fortinet.com/blog/threat-research/pokemon-go-plus-preview-through-reverse-engineering) and [ar5hil's pgpemu-1](https://github.com/ar5hil/pgpemu-1) informed the result-pattern heuristic. The upstream [BSD 2-Clause license](../LICENSE) and copyright notice are retained. This is not an official Pokémon GO or ESP-IDF project.

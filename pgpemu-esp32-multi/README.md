# ESP32-WROOM-32版 PGP-Badge-Multi

[日本語](#日本語) | [English](#english)

[設計書 / Design](DESIGN.md) — 接続別状態・通知処理・診断ログの内部構造 / Per-client state, notification scheduling, and diagnostic storage.

## 日本語

### これは何？現在の状態は？

初代Pokémon GO Plusを模したBLEファームウェアです。元の1台版は`../pgpemu-esp32`に残し、このディレクトリで最大3台の接続を扱います。ESP32-C3用ではありません。GPIO2の青色LEDは接続台数の確認に使います。

元の1台版はESP32-WROOM-32とiPhone版Pokémon GOで接続済み表示まで確認しています。**この複数端末版もESP-IDF 4.4.8でビルド・書き込みに成功し、ログ上は3接続とも認証処理の終点まで進みました。青色LEDの接続台数表示は実機で確認できています。** 3台のPokémon GO画面での同時接続、iPhoneとAndroidの混在、1台切断後の独立性は引き続き確認が必要です。

### 0. 準備するもの

2026-10-10の修正版は実機への書き込み・起動に成功し、`Persistent diagnostics enabled: 512 KiB, previous boot retained`と`advertising start successfully`を確認しました。3台使用時の停止症状への効果はこれから実機で確認します。

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

実行ポリシーのエラーが出た場合は、同じPowerShellで次を実行してから続けます。管理者権限は不要で、このウィンドウを閉じると設定は戻ります。パスを引用符で囲むだけでは実行されないので、先頭の`&`も必要です。

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
& "C:\esp\v4.4.8\esp-idf\export.ps1"
idf.py -p COM3 flash monitor
```

`export.ps1`はそのPowerShellセッションでESP-IDFのPython環境と各ツールを使えるようにします。実行ポリシーでブロックされた場合に限り、そのセッションだけ`Set-ExecutionPolicy -Scope Process Bypass`を実行し、`export.ps1`を再実行してください。`idf.py`が見つからない場合は`export.ps1`が成功したか確認します。`set-target esp32`はESP32-WROOM-32を選ぶ操作で、ESP32-C3を選ばないでください。

`CONFIG_BTDM_CTRL_BLE_MAX_CONN`は3以上が必要です。`sdkconfig.defaults`に3を指定しています。以前の設定を持ち込んだ場合は、ビルド前に生成された`sdkconfig`の値を確認してください。ESP-IDFの版やターゲットを切り替えた場合は`idf.py fullclean`後に再ビルドします。`build`内のバイナリにはSecretが埋め込まれるため、Releaseなどで配布しないでください。

### 4. ESP32へ書き込み、ログを見る

開発ボードをUSB接続し、Windowsの「デバイスマネージャー → ポート（COMとLPT）」でCOM番号を確認します。`COM3`は例です。

```powershell
idf.py -p COM3 flash monitor
```

`flash`がESP32へ書き込み、`monitor`が起動後のシリアルログを表示します。この複数端末版のCOM3への書き込みは実機で成功しています。モニターを閉じるには`Ctrl+]`を押します。ポートが見つからなければUSBケーブルがデータ通信用か、ドライバーが入っているかを確認します。ポートが使用中なら他のシリアルモニターを閉じます。自動書き込みが始まらないボードでは、画面に`Connecting...`が出たときにBOOTボタン操作が必要な場合があります。

以前のWROOM-32実機試験では、初回に`Wrong boot mode detected (0x13)`が出て書き込みに失敗しました。これはビルドエラーではありません。再実行時に**BOOTを押したまま書き込み開始 → `Connecting...`が出たら離す**ことで書き込めました。成功時は`Hash of data verified`と`Hard resetting via RTS pin`が目安です。起動時にBOOTを押す必要はありません。

### 5. 青色LEDと接続ログを確認する

開発ボードの**青色GPIO2 LEDのみ**をソフトウェアで制御します。赤色LEDは電源表示で制御できません。HIGHで点灯するボード用です。LOWで点灯するボードなら`main/pgpemu.c`の`STATUS_LED_ACTIVE_LEVEL`を`0`にして再ビルドします。

| 状態 | 青色LED |
| --- | --- |
| 接続0台・Advertising中 | 点灯 |
| 接続1台 | 短く1回＋長く1回、繰り返し |
| 接続2台 | 短く2回＋長く1回、繰り返し |
| 接続3台 | 短く3回＋長く1回、繰り返し |

接続台数の点灯・点滅は実機で確認できました。青LEDは接続台数を示します。

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

`cert_state=... -> 6`はファームウェア側の認証手順が終点に進んだ目安です。**Pokémon GOの画面でも接続済みか確認**してください。Androidで長いGATT書き込みが起きると`prepare write conn_id=...`と`exec write conn_id=...`が表示されます。

iPhoneを接続したままAndroid、その後3台目を接続し、全端末の接続表示とLEDの1→2→3台パターンを確認します。1台だけ切断したら、その`conn_id`の`Client disconnected`と接続数の減少、Advertising再開を確認します。残りの端末がPokémon GO上で接続済みのままか確かめ、切断した端末を再接続します。ログには機器アドレスや認証データが含まれ得るため、共有前に伏せてください。

### 6. PCなしで診断ログを残し、帰宅後に取り出す

3台使用中に全端末で自動操作が止まり、ESP32の再起動で復帰する症状を調査するため、本体フラッシュに診断ログを保存します。修正版では通知キューを端末別に分け、混雑中の待機と期限付き再試行を行います。接続番号が再利用されても以前の接続の操作は破棄します。症状の解消は実機ログで確認します。

既存の環境で更新する場合は、改修版フォルダーで次を実行してください。ESP-IDF環境が有効なPowerShellを使います。

```powershell
idf.py menuconfig
idf.py build
idf.py -p COM3 flash monitor
```

`menuconfig`では**Partition Table → Partition Table → Custom partition table CSV**を選び、ファイル名を`partitions.csv`にして保存してからビルドします。古い`sdkconfig`がある場合、`sdkconfig.defaults`だけでは既存設定を上書きしません。起動時の`PGP_DIAG: Persistent diagnostics enabled: 512 KiB, previous boot retained`を確認したら、`Ctrl+]`でモニターを終了して外へ持ち出せます。この版は**アプリだけでなくパーティションテーブルも含む通常の`flash`**で書き込んでください。NVSとアプリの配置は従来と同じです。

記録するのは接続・切断理由、認証状態の進行、通知購読、未知のLEDコマンド、認証通知・再試行・期限切れ、混雑状態です。通常のボタン送信と既知のLEDコマンドは端末別に集計し、接続中は60秒ごとに`TX_HEALTH`を残します。空きメモリとBLEイベント数の`HEARTBEAT`は接続中60秒、未接続時600秒間隔です。認証のMAC・Device Key・Blob・session keyや認証ペイロードは保存しません。`NOTIFY_SENT`はBLEスタックに渡せたという意味で、捕獲成功の判定ではありません。

問題が出たらESP32を再起動してかまいません。512KBを2つの領域に分け、再起動するたびに保存先を切り替えます。各領域は直近約1,024件を循環保存し、再起動前の領域は次の起動中に上書きしません。ただし再起動を重ねると古い起動の記録は上書きされます。電源を落とした瞬間にRAMキューに残っていた記録や、書き込み途中の最後の記録は失われる場合があります。`HEARTBEAT`内の`lost`はRAMキューが満杯で保存できなかった件数です。

帰宅後、USB接続し、**シリアルモニターを閉じて**同じESP-IDF PowerShellで実行します。

```powershell
python .\tools\download-diagnostics.py --port COM3
```

診断領域だけを読み、`diagnostics-年月日-時分秒.txt`を現在のフォルダーに作ります。読み出しには少し時間がかかり、ESP32は前後でリセットされます。接続に失敗する場合は、書き込み時と同じBOOT操作を試してください。症状が出たおおよその時刻と、このテキストを解析用に渡してください。診断ログはGitの公開対象外です。取得前に`erase-flash`を行うと保存ログも消えるので実行しないでください。

### 7. Watchdogによる監視と自動復旧

2026-10-11にソフトウェア監視を追加しました。ビルド確認と実機動作確認は別で、この版の自動復旧と複数端末での長時間運用は実機検証が必要です。通常どおり`idf.py build`、`idf.py -p COM3 flash monitor`で更新します。Secretや保存ログを消す`erase-flash`は不要です。

| 監視対象 | 復旧動作 |
| --- | --- |
| 通知タスク・監視タスクが10秒間進まない | タスクwatchdogで本体再起動。両CPUのidle監視も継続 |
| 実行中のBLEコールバックが10秒戻らない | 本体再起動。BLEイベントが来ないだけでは発動しない |
| 接続・認証開始から120秒で認証未完了、または認証購読中に状態が45秒進まない | 該当端末だけ切断して再接続待ち |
| 認証通知1件の送信失敗が確定、または30秒の集計窓内でボタン通知3件が失敗 | 該当端末だけ切断。即時API失敗は従来どおり期限まで再試行し、期限切れ・満杯・非同期失敗を監視へ報告 |
| 空きスロットがあるのにAdvertisingが開始できない | 停止確認後5秒、開始待ちなら20秒で再試行対象。監視側の再試行間隔は20秒以上 |
| 全接続で切断要求が120秒完了しない | 本体再起動。他に正常な接続があれば本体を再起動しない |
| BLE初期設定が120秒終わらない、または接続0台でAdvertising復旧が120秒以上進まない | 本体再起動 |

認証済み端末の単なる無通信、ポケモンがいないこと、捕獲結果が来ないことは切断・再起動条件にしません。スマホの携帯回線状態やゲームサーバーの進行はESP32から直接監視できません。また、端末別の復旧は相手アドレスを指定するため、古い`conn_id`を別の端末が再利用してもその別端末を切断しません。

起動時に`PGP_GUARD: Watchdog enabled: tasks=10s auto_restart=1`を確認してください。診断ログには`GUARD_BOOT`、`GUARD_CLOSE`、`GUARD_ADV_RETRY`、`GUARD_RESTART`を残します。watchdog停止時は直前のログが保存できない場合がありますが、次の起動のリセット理由と、監視タスクが計画した再起動理由はRTCメモリも使って記録します。

短時間の自動再起動が3回続くと、その起動中はタスクwatchdogの強制再起動と監視タスクの本体再起動を抑止します。端末別復旧とAdvertising再試行は継続します。10分経過して既知の異常がない場合は次回起動向けの回数をリセットしますが、その起動中の抑止は解除しません。通常はUSBだけでなく別の電源も含めて電源を一度切ると回数がリセットされます。ハードウェアの割り込みwatchdog、panic、電源不良によるリセットまではこの抑止の対象にできません。

ESP32はAdvertisingを再開できますが、**スマホへ再接続を強制することはできません**。アプリ側が自動で戻らなければPokémon GOの接続ボタンを押してください。本体再起動時は全端末が一度切断されます。LEDは引き続き接続台数だけを表示します。

### 8. 自動操作・認証・ログの改善（2026-10-11）

自動ボタン応答は、既知のポケモン・未登録ポケモン・ポケストップの操作要求だけへ返します。既知の捕獲結果、逃走、回収結果、バッグ満杯・ボックス満杯・ボール不足等の表示、消灯には返しません。判別できないパターンは取りこぼし防止のため従来どおり応答し、`LED_UNKNOWN`を保存します。これはゲームが送るLEDパターンの判別であり、GPIO2で4回・5回点滅させる機能ではありません。

認証通知をキューへ入れられないときは認証状態を先へ進めず、監視による該当端末だけの復旧につなげます。キュー受付は実際の通信成功を保証しないため、その後の送信期限・非同期失敗も監視します。Advertisingの定期再試行は前節の監視が担当します。

`TX_HEALTH`の`cc`は認証／ボタンの購読、`busy`は混雑、`q`はキュー残数、`p`は保留通知です。`sent`は通常ボタンのAPI受付数、`req`は既知の操作要求数、`caught`・`fled`・`spun`はLEDパターンから推定した結果数、`err`・`off`・`unk`はエラー表示・消灯・未知パターン数です。カウンターは接続・認証世代ごとの累計で、切断時にも`CLIENT_SUMMARY`を残します。ゲーム画面上の実際の捕獲数を保証するものではありません。

保存時は書き込み後に読み戻して照合します。取得ツールの`non_record`は診断レコードの識別子がない領域、`crc_bad`は識別子があるのにCRCが合わないレコードです。両者を保存破損として一括扱いしません。実機ログを再調査した結果、以前の「CRC不一致736件」は識別子のない736領域で、CRC不一致は0件でした。旧データ等が残っている可能性があるため、診断領域の生バイナリも公開しないでください。

CコードのLED分類・認証遷移と診断デコーダーをホスト上でテストしています。変更後の捕獲・2〜3台運用・自動復旧は実機での確認が必要です。まず2台で試し、問題の時刻と診断ログを残してください。スマホの再接続はPokémon GO側の操作が必要な場合があります。

### 謝辞・ライセンス

LEDコマンドの判別には[spezifisch/pgpemu](https://github.com/spezifisch/pgpemu/blob/master/pgpemu-esp32/main/pgp_led_handler.c)の知見を利用しています。Yohanes Nugroho氏、spezifisch氏にも感謝します。この部分のBSD 2-Clause著作権・ライセンス表示は[main/LICENSE.LED](main/LICENSE.LED)に保持しています。

[Yohanes Nugroho氏](https://github.com/yohanes/pgpemu)の解析と元コード、[bentomo氏とPGP-Badgeの貢献者](https://github.com/bentomo/PGP-Badge)のハードウェア／ESP32実装、[Jesus Bamford氏のSuota Go+](https://github.com/Jesus805/Suota-Go-Plus)の公開情報に感謝します。元コードの[BSD 2-Clauseライセンス](../LICENSE)と著作権表示を保持しています。Pokémon GOやESP-IDFの公式プロジェクトではありません。

---

## English

### Purpose and status

This ESP32-WROOM-32 firmware emulates an original Pokémon GO Plus for up to three BLE clients. The original single-client version remains in `../pgpemu-esp32` and previously reached "connected" in Pokémon GO on an iPhone. This is not for ESP32-C3. The multi-client firmware **builds and flashes under ESP-IDF 4.4.8**. Logs show three connections progressing through authentication, and the GPIO2 blue LED's connection-count pattern was observed. Connected status in all three apps, mixed iPhone/Android operation, and disconnect isolation still need confirmation.

### 0. What you need

On 2026-10-10, the revised firmware was flashed and booted successfully on hardware, reporting `Persistent diagnostics enabled: 512 KiB, previous boot retained` and `advertising start successfully`. Its effect on the three-client stall still requires field testing.

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

For the execution-policy error, run the following in the same PowerShell window. Administrator privileges are not required; the policy change ends when that window closes. Keep the leading `&`: quoting a path alone only prints it.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
& "C:\esp\v4.4.8\esp-idf\export.ps1"
idf.py -p COM3 flash monitor
```

### 4. Flash and monitor

Connect the board by USB. Find its COM port in Windows Device Manager under **Ports (COM & LPT)**. Replace example `COM3` below:

```powershell
idf.py -p COM3 flash monitor
```

`flash` uploads to the ESP32; `monitor` displays serial logs. Flashing this multi-client variant on COM3 succeeded in a hardware test. Exit monitor with `Ctrl+]`. If no port appears, check cable and USB-serial driver. If the port is busy, close other serial monitors. Some boards need the BOOT button when `Connecting...` appears.

In a previous **single-client WROOM-32** trial, the first attempt failed with `Wrong boot mode detected (0x13)`; this was not a build error. Retrying while holding **BOOT**, then releasing it when `Connecting...` appeared, succeeded. `Hash of data verified` and `Hard resetting via RTS pin` indicate a successful flash. Normal boot does not require BOOT.

### 5. LED and connection checks

Only the **blue GPIO2 LED** is software-controlled; the board's red LED is a power indicator. The default assumes HIGH turns blue on. For active-low boards, set `STATUS_LED_ACTIVE_LEVEL` in `main/pgpemu.c` to `0` and rebuild.

| State | Blue LED |
| --- | --- |
| 0 clients, advertising | Steady on |
| 1 / 2 / 3 clients | 1 / 2 / 3 short flashes, then one long flash, repeating |

The connection-count indicator has been observed on hardware. The blue LED indicates the number of connected clients.

Look for `advertising start successfully`, then `Client connected: conn_id=... slot=0`, `Active clients: 1/3`, and `Advertising restarted for next client`. The firmware's authentication progression should reach `conn_id=... cert_state=2 -> 6` (or `5 -> 6` on reconnect), **and Pokémon GO must also show connected**. Android long writes may log `prepare write conn_id=...` and `exec write conn_id=...`.

Keep the iPhone connected, add Android, then a third phone; expect distinct `conn_id` and slots 0, 1, and 2, with `Active clients: 3/3`. Disconnect only one, check the remaining phones stay connected, advertising resumes, and the disconnected phone can reconnect. Redact addresses and authentication traffic before sharing logs.

### 6. Record diagnostics without a PC and retrieve them later

Persistent diagnostics help investigate all three clients stopping until an ESP32 restart. The revised sender uses per-client notification queues, congestion handling, bounded retries, and connection generations to discard stale operations. Field testing is still needed to establish whether these changes resolve the symptom.

Activate ESP-IDF in PowerShell, then run in this project directory:

```powershell
idf.py menuconfig
idf.py build
idf.py -p COM3 flash monitor
```

In `menuconfig`, select **Partition Table → Partition Table → Custom partition table CSV**, set its filename to `partitions.csv`, and save. Defaults do not override an existing configuration. Confirm `PGP_DIAG: Persistent diagnostics enabled: 512 KiB, previous boot retained`, then exit with `Ctrl+]`. Use the full `flash` command to update both the application and partition table. Existing NVS and application offsets are preserved.

Records contain connection/disconnection reasons, authentication progression, subscriptions, unknown LED commands, certificate notifications, retries, expiry and congestion. Normal button sends and known LED patterns are aggregated into per-client `TX_HEALTH` reports every 60 seconds while connected. Memory/BLE-event `HEARTBEAT` intervals are 60 seconds connected and 600 seconds idle. Records exclude MAC addresses, Device Key, Blob, session keys and authentication payloads. `NOTIFY_SENT` means accepted by the BLE stack, not successful capture.

The 512-KiB partition has two circular banks, each holding roughly 1,024 recent records. Rebooting switches banks and preserves the previous boot's bank during the next run. Further reboots eventually overwrite older boots. Pending RAM records and a partially written final record may be lost on power removal. Heartbeat `lost` counts records dropped because the RAM queue was full.

At home, connect USB, **close any serial monitor**, and use the ESP-IDF PowerShell:

```powershell
python .\tools\download-diagnostics.py --port COM3
```

The command reads only the diagnostic partition and creates a timestamped `diagnostics-*.txt` in the current directory. Reading takes some time and resets the board. If connection fails, use the same BOOT procedure as for flashing. Share the text and approximate incident time for analysis. Diagnostic exports are Git-ignored. Do not run `erase-flash` before retrieval, as it also erases the logs.

### 7. Watchdog monitoring and automatic recovery

Software supervision was added on 2026-10-11. Build checks do not establish hardware recovery behavior or long-duration multi-client stability. Update with `idf.py build` and `idf.py -p COM3 flash monitor`; do not use `erase-flash`.

| Condition | Action |
| --- | --- |
| Notification/supervisor task fails to complete a pass for 10 s | Task watchdog restarts the ESP32; both CPU idle tasks remain monitored |
| An executing BLE callback does not return for 10 s | Restart; absence of BLE events alone does not trigger recovery |
| Authentication incomplete 120 s after connection/authentication restart, or no state progress for 45 s while certificate notifications are enabled | Disconnect only the affected peer |
| One failed certificate notification, or three failed button notifications within a 30-s counting window | Disconnect that peer; immediate API failures retain existing bounded retries, with expiry, overflow and asynchronous failures reported to supervision |
| Advertising missing with spare capacity | Eligible after 5 s inactive or 20 s pending; supervisor retries are at least 20 s apart |
| Every remaining client's close request remains incomplete for 120 s | Restart; one unhealthy peer does not trigger this restart while another link remains healthy |
| BLE setup incomplete for 120 s, or advertising recovery stuck for 120 s with zero clients | Restart |

Authenticated-client inactivity, absence of Pokémon, and missing capture results are not faults. The ESP32 cannot directly monitor the phone's internet connection or server progress. Peer recovery targets its Bluetooth address, not a potentially reused connection ID.

Look for `PGP_GUARD: Watchdog enabled: tasks=10s auto_restart=1` at boot. Persistent records include `GUARD_BOOT`, `GUARD_CLOSE`, `GUARD_ADV_RETRY`, and `GUARD_RESTART`. The final records before a watchdog reset may be lost; the next boot records reset reason and uses retained RTC metadata for supervisor-planned restart causes.

After three consecutive automatic resets, task-watchdog panic and supervisor global restarts are suppressed for that boot. Peer recovery and advertising retries continue. After ten minutes without a known fault, the next-boot reset counter is cleared, but suppression remains for the current boot. Removing all board power normally clears the retained counter. Interrupt-watchdog, unrelated panic, and power-failure resets are outside this suppression mechanism.

The ESP32 can resume advertising but **cannot force the phone to reconnect**. Tap the connection control in Pokémon GO if the app does not reconnect. A global reset disconnects every phone. The blue LED continues to show connection count only.

### 8. Automatic actions, authentication and logging improvements (2026-10-11)

Automatic buttons now respond to known Pokémon, new-Pokémon and PokéStop requests, not known catch/flee/spin results, bag/box/ball/range errors or off commands. Unknown valid patterns retain the legacy button fallback and generate `LED_UNKNOWN` for investigation. This classifies the app's protocol patterns; it does not add four/five-flash GPIO2 displays.

Authentication state advances only when its notification is accepted into the queue. Rejection leads into affected-peer recovery. Queue acceptance does not guarantee delivery: subsequent expiry and asynchronous failures are still supervised. Periodic advertising recovery is handled by the watchdog section above.

In `TX_HEALTH`, `cc` is certificate/button subscription status, `busy` congestion, `q` queue depth and `p` a pending notification. `sent` counts accepted button sends; `req` known requests; `caught`/`fled`/`spun` inferred LED results; `err`/`off`/`unk` error/off/unknown patterns. Counters accumulate per connection/authentication generation and also appear in `CLIENT_SUMMARY` at disconnect. They are not independently verified game-screen capture totals.

Each flash record is read back and compared after writing. Export reports distinguish `non_record` (missing diagnostic magic) from `crc_bad` (recognized record with a CRC mismatch). Rechecking the field dump found 736 non-record slots and zero CRC failures, not 736 damaged records. Old data may remain in unused sectors: do not publish raw diagnostic binaries either.

Host tests exercise the production LED classifier, authentication transition control flow and decoder. Capture behavior, two/three-phone operation and recovery after this change still need hardware verification. Start with two phones and retain diagnostics with approximate fault times. Phone reconnection may require a Pokémon GO action.

### Credits and license

LED classification draws on [spezifisch/pgpemu](https://github.com/spezifisch/pgpemu/blob/master/pgpemu-esp32/main/pgp_led_handler.c). Thanks also to Yohanes Nugroho and spezifisch; the BSD 2-Clause copyright/license notice for this portion is retained in [main/LICENSE.LED](main/LICENSE.LED).

Thanks to [Yohanes Nugroho](https://github.com/yohanes/pgpemu) for the original reverse engineering and code, [bentomo and PGP-Badge contributors](https://github.com/bentomo/PGP-Badge) for hardware and ESP32 firmware, and [Jesus Bamford](https://github.com/Jesus805/Suota-Go-Plus) for the extraction project. The upstream [BSD 2-Clause license](../LICENSE) and copyright notice are retained. This is not an official Pokémon GO or ESP-IDF project.

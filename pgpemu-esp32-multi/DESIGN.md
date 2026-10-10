# PGP-Badge-Multi 設計書 / Design

対象 / Scope: ESP32-WROOM-32, ESP-IDF 4.4.8, 最大3 BLE接続 / up to three BLE connections.

利用開始とログ取得の操作は[README](README.md)を参照してください。この文書は現行実装の構造と制約を説明します。

For setup and log retrieval, see the [README](README.md). This document describes the current implementation and its limitations.

## 1. 検証状況 / Verification status

- 元の1台版はiPhoneのPokémon GO上で接続済み表示を確認済み。複数端末版では以前のログで3接続の認証状態進行を確認済み。
- 3台で自動操作が止まる・接続できないことがあり、ESP32再起動で復帰するという利用者報告を受けて、通知処理と診断記録を改修。
- 2026-10-10、修正版 `6d9ed91` 系列のビルド、COM3への書き込み、実機起動、512KiB診断機能の初期化、Advertising開始を確認。診断デコーダーの単体テスト4件も成功。
- 修正版での長時間3台運用、iOS/Android混在、切断後の他端末維持、保存ログの実機回収、停止症状の解消は未確認。起動成功と安定動作を区別する。

The original single-client firmware reached connected status in Pokémon GO on iPhone; earlier multi-client logs showed three authentication state progressions. Reported three-client stalls and intermittent connection failures motivated the notification and diagnostics changes. On 2026-10-10, the `6d9ed91` revision was built, flashed on COM3, booted, initialized 512-KiB diagnostics, and started advertising. Four decoder unit tests passed. Long-duration three-client operation, mixed iOS/Android behavior, disconnect isolation, hardware log retrieval, and resolution of the stall remain unverified.

## 2. 接続の所有権 / Connection ownership

`main/pgpemu.c`の固定3スロット `clients[]` を`conn_id`で検索する。CONNECTで空きスロットを確保し、DISCONNECTで該当スロットのprepare領域を解放して状態を消去する。残りのスロットは消去しない。接続数が3未満ならAdvertisingを開始・維持する。開始済み／開始要求中を区別し、非同期開始失敗の再試行は最大3回とする。

`main/pgpemu.c` owns three fixed `clients[]` slots, looked up by `conn_id`. CONNECT allocates a free slot; DISCONNECT frees only that slot's prepare buffer and state. Advertising runs when capacity remains. Active and pending advertising states are distinct; asynchronous start failures have up to three retries.

| 接続別に保持 / Per-client state | 用途 / Purpose |
| --- | --- |
| `conn_id`, `gatts_if`, `remote_bda`, `used` | イベントと送信先の識別 / Event routing and destination |
| `cert_state`, `session_key`, `cert_buffer`, `cert_len` | 認証状態と専用READ応答 / Authentication and private READ response |
| `reconnect_challenge`, `has_reconnect_key` | 接続中の再認証情報 / Reauthentication data within a connection |
| `cert_notify_enabled`, `button_notify_enabled` | 証明書・ボタンの独立したCCCD / Separate certificate and button subscriptions |
| `prepare_write_env` | handle、長さ、バッファ / Handle, length, buffer |
| `generation`, `congested` | 古い通知の無効化と混雑情報 / Stale-work rejection and congestion hint |

切断すると再認証情報も消去する。端末ごとの永続的な高速再接続キャッシュではない。

Reauthentication data is cleared on disconnect; this is not a persistent per-phone fast-reconnect cache.

## 3. GATT READ / WRITE

証明書READは発生元`conn_id`の`cert_buffer`と長さから応答し、共有attribute valueへ認証応答を書き込まない。CCCDもアプリ応答方式とし、その接続の有効状態を返す。WRITEとEXEC WRITEの処理は発生元接続へ振り分ける。

Prepare writeは接続別バッファへ格納する。offset・最大長・同一handleを確認し、現在の長さを超える穴あき書き込みは拒否する。EXECまたはキャンセル後にそのバッファを解放する。認証の各段階でも必要な入力長を検証する。

Certificate READ responses use the originating client's buffer and length, not shared attribute storage. CCCD READ responses also report that client's subscription state. WRITE and EXEC WRITE route by the originating connection. Prepare-write storage is per-client, validates offsets, bounds and handle consistency, rejects gaps, and is freed after execution or cancellation. Authentication stages check required input lengths.

## 4. 通知と自動操作 / Notifications and automatic actions

LEDコマンド由来の自動ボタン応答は発生元端末だけへ送る。UART手動操作はボタン通知を購読中の端末へ配信する。通知を購読していない端末へ送信しない。

Automatic button responses to LED commands target only their originating client. UART manual actions broadcast to button subscribers. Notifications require the corresponding CCCD to be enabled.

| 設定 / Setting | 現行値 / Current value |
| --- | --- |
| 通知キュー / Notification queue | 各スロット16件、証明書とボタンを同じFIFOで管理 / 16 entries per slot, shared FIFO for certificate and button |
| ワーカー / Worker | 優先度8、20ms周期、各スロットを順に処理 / Priority 8, 20-ms loop, each slot gets a turn |
| API失敗後 / After immediate API failure | 通常100ms、混雑時200ms後に再試行 / Retry after 100 ms, or 200 ms with congestion hint |
| 有効期限 / Lifetime from enqueue | 証明書8秒、ボタン2秒 / Certificate 8 s, button 2 s |

BLEコールバックはキュー満杯時に待たず、破棄を記録する。ワーカーはスロットごとに1件を保留し、他端末の処理を続行する。接続と認証開始に世代番号を付け、切断・再接続・認証再開始後の古い世代の通知を破棄する。送信直前に購読と期限も再確認する。共有する送信メタデータだけを短いクリティカルセクションでコピーし、BLE送信APIはその外で呼ぶ。

Callbacks never wait for queue space. The worker holds one pending item per slot and lets other slots proceed. Generation numbers invalidate old queued work after reconnect or authentication restart; subscriptions and expiry are rechecked before sending. A short critical section copies shared destination metadata; BLE send calls run outside it.

`NOTIFY_SENT`はAPIが受理した意味で、無線到達・ゲームでの処理・捕獲成功の保証ではない。非同期送信失敗は`NOTIFY_ASYNC_FAIL`として記録するが、この経路の自動再送はしない。混雑解除イベントを取り逃しても永久待機しないよう、バックオフ後にAPIへ再度問い合わせる。

`NOTIFY_SENT` means API acceptance, not radio delivery, game processing, or capture success. Asynchronous failures are logged as `NOTIFY_ASYNC_FAIL` and are not automatically retried. A stale congestion hint does not block sending forever: the API is checked again after backoff.

## 5. 青LED / Blue LED

GPIO2のみを制御する。0接続は点灯、1〜3接続は台数分の短い点滅＋長い点滅を繰り返す。表示はBLE接続数であり、認証成功数や捕獲数ではない。4回・5回の結果表示は仕様に含めない。赤LEDはボードの電源表示で制御しない。

Only GPIO2 is controlled. Zero connections shows steady blue; one to three connections show that many short flashes followed by a long flash. This counts BLE connections, not authenticated clients or captures. Four/five-flash result patterns are not part of the specification. The red power LED is not software-controlled.

## 6. 保存診断 / Persistent diagnostics

`main/pgp-diag.c`が診断専用データを記録する。一般のシリアルログを丸ごと保存する方式ではない。32件のRAMキューから優先度2のタスクがフラッシュへ書く。キュー満杯時は待たずに破棄し、`lost`を増やす。フラッシュ書き込みエラー時は記録タスクを停止してエラーを表示する。

`main/pgp-diag.c` records explicitly selected metadata, not the entire serial stream. A priority-2 task drains a 32-record RAM queue to flash. A full queue drops records and increments `lost`. Flash errors stop recording and produce an error message.

| 領域 / Partition | Offset | Size |
| --- | --- | --- |
| `nvs` | `0x9000` | `0x6000` |
| `phy_init` | `0xf000` | `0x1000` |
| `factory` | `0x10000` | `0x100000` |
| `pgp_diag` | `0x110000` | `0x80000` (512 KiB) |

`partitions.csv`を使用する。従来のNVSとアプリの配置を維持し、パーティションテーブルを含む通常の`flash`で導入する。実機は4MiBフラッシュを検出したが、今回のビルド設定は2MiBである。この配置は2MiB内に収まる。起動時のサイズ警告だけでは書き込み失敗を意味しない。

Use `partitions.csv` and full `flash`, including the partition table. Existing NVS/application offsets are preserved. The tested board detected 4-MiB flash while the image was configured for 2 MiB; this layout fits within 2 MiB. That size warning alone does not indicate a failed flash.

保存領域は256KiBの2バンク。現在のバンク内を循環使用し、起動時に最新の正常レコードを探して反対側のバンクへ切り替える。1バンクは1,024件で、直前起動のバンクは次の起動中に上書きしない。再起動を重ねると古い記録は失われる。保存可能な時間はイベント量次第であり、一日分の保証ではない。再利用バンクには消去前の古いレコードが一部残る場合がある。

Two 256-KiB circular banks hold 1,024 records each. Boot scans valid records, then switches to the bank opposite the newest record. The previous boot's bank is preserved during the next run; further reboots overwrite older history. Retention duration depends on event rate, not a guaranteed number of hours. Reused banks may retain older records until those sectors are erased.

レコードはlittle-endianの256byte固定長：magic（4）、sequence（4）、起動後ms（8）、message（236）、CRC32（4）。CRCは先頭252byteを対象にする。電源断で未保存のRAM記録や書き込み途中のレコードは失われる。デコーダーは不正CRCを除外し、sequence順に並べる。時刻は起動後の経過時間で、実時刻ではない。

Each 256-byte little-endian record contains magic (4), sequence (4), uptime milliseconds (8), message (236), and CRC32 (4), computed over the first 252 bytes. Pending RAM records or interrupted writes may be lost on power removal. The decoder rejects invalid CRCs and sorts by sequence. Uptime is not wall-clock time.

### 解析時の目印 / Analysis markers

| 記録 / Record | 見る内容 / Meaning |
| --- | --- |
| `BOOT` | 再起動理由・版・保存バンク / Reset reason, version, bank |
| 接続・認証・`CCCD` / Connection, auth, CCCD | どの接続がどこまで進んだか / Per-connection progression |
| `LED_UNKNOWN`, `TX_HEALTH`, `CLIENT_SUMMARY` | 未知パターン詳細と既知の要求・結果の集計。ゲーム画面での成功確認ではない / Unknown patterns and aggregate inferred requests/results, not independently verified game outcomes |
| `NOTIFY_TRY`, `NOTIFY_SENT`, `NOTIFY_RETRY` | 送信開始・API受理・即時失敗 / Start, API acceptance, immediate failure |
| `NOTIFY_DROP`, `NOTIFY_QUEUE_FULL`, `NOTIFY_ASYNC_FAIL` | 期限・世代・購読・満杯・非同期失敗 / Expiry, generation, subscriptions, overflow, async failure |
| `CONGEST`, `TX_HEALTH` | 接続別の混雑・残キュー・保留 / Per-client congestion, queue and pending item |
| `HEARTBEAT` | 接続中60秒・未接続600秒ごとの接続数、Advertising、メモリ、イベント数、欠落数 / Client, advertising, memory, event and loss summary every 60 s connected / 600 s idle |

回収は`tools/download-diagnostics.py --port COM3`をESP-IDFのPythonで実行する。診断領域だけを読み、テキスト化する。操作前後のリセットがあるため、不要な再起動を重ねず早めに回収する。取得前の`erase-flash`は禁止。単体テストは`python -m unittest discover -s tools -p test_diagnostics.py`。

Run `python tools/download-diagnostics.py --port COM3` in the ESP-IDF environment to read only the diagnostic partition and decode it. Retrieval resets the board; retrieve promptly and avoid unnecessary intervening reboots. Do not run `erase-flash` first. Decoder tests: `python -m unittest discover -s tools -p test_diagnostics.py`.

## 7. 公開境界と残る制約 / Publication boundary and limitations

実機用`main/secrets.c`、抽出JSON、実機用ビルド成果物、診断のエクスポートは公開しない。診断にはMAC、Device Key、Blob、session key、認証ペイロードを記録しない。ただし通常のシリアルログには機器情報や認証データが出る場合があるため、そのまま公開しない。

Do not publish real `main/secrets.c`, extracted JSON, device-specific build artifacts, or diagnostic exports. Persistent records exclude MAC addresses, Device Key, Blob, session keys and authentication payloads. Ordinary serial output may contain device/authentication information and must be reviewed before sharing.

本変更は停止の原因を実機で確定したものではない。通知期限切れや非同期送信失敗は接続・認証のやり直しを必要とする場合がある。Advertising開始APIの即時失敗に対する周期的な復旧は、以下の監視モジュールが補う。世代番号の再確認と送信API呼び出しは一体の原子的操作ではない。フラッシュ記録によるBLEタイミングへの影響も実機評価が必要。これらは今後のログ解析と検証対象とする。

The root cause of the stall has not been established on hardware. Expired authentication notifications or asynchronous failures may require reconnecting. The supervisor below supplies periodic recovery after immediate advertising-start API failures. Generation validation and the subsequent send API call are not one atomic operation. Flash logging's impact on BLE timing also needs hardware evaluation. These remain targets for field logs and further testing.

## 8. Watchdogと段階復旧 / Watchdog and staged recovery

2026-10-11追加の`main/pgp-guard.c`は、接続管理用構造体そのものではなく、短いクリティカルセクションで保護する監視専用コピーを持つ。BLEコールバックが接続・世代・認証状態・購読・Advertising状態を更新し、通知処理が確定した送信失敗を報告する。監視タスクは優先度4、1秒周期。BLE API呼び出しはロック外で行う。

Added on 2026-10-11, `main/pgp-guard.c` owns supervision-only copies protected by short critical sections. BLE callbacks update lifecycle/authentication/advertising state; notification paths report definitive failures. The priority-4 supervisor runs once per second and calls BLE APIs outside the lock.

通知タスクと監視タスクはタスクwatchdogへ個別登録し、一巡した後だけ更新する。既存`sdkconfig`でも実行時に10秒・panic有効を設定する（再起動抑止時はpanic無効）。両CPUのidle監視は既存設定を維持する。BLEコールバックは開始／終了を記録し、実行中のコールバックが10秒戻らない場合は監視タスクが本体再起動を要求する。無イベントの時間は監視対象にしない。

Notification and supervisor tasks subscribe individually to the task watchdog and feed it only after a completed pass. Runtime configuration sets a 10-s timeout and enables panic unless reset-loop protection is active; existing CPU idle subscriptions remain. Callback entry/exit tracking detects a callback executing for 10 s, not absence of incoming events.

接続または認証再開始から120秒で認証未完了、認証購読中に状態が45秒進まない、認証通知が1件失敗、ボタン通知が30秒の窓内で3件失敗、のいずれかで端末別復旧へ進む。失敗はキュー満杯、送信期限切れ、非同期エラーが対象。即時APIエラーは通知タスクが有効期限まで再試行する。購読OFF・古い世代の通知破棄は異常に数えない。認証済み端末の無通信時間は制限しない。

Peer recovery triggers on incomplete setup after 120 s, stalled subscribed authentication after 45 s, one failed certificate notification, or three failed button notifications within a 30-s counting window. Overflow, expiry and asynchronous errors count; immediate API errors retain bounded sender retries. Disabled subscriptions and stale generations do not count as faults. Authenticated inactivity has no timeout.

監視スロットの世代を再確認した上で、相手アドレス指定の`esp_ble_gap_disconnect`を呼ぶ。接続番号の再利用によって別端末を切断しないためである。同じ端末が検証直後に再接続する競合はなおあり得る。切断要求は15秒間隔で最大3回。全ての残存接続で最初の切断要求から120秒経っても切断が完了しない場合に限り、この経路で本体を再起動する。正常な別接続がある場合は本体を再起動しない。

After generation validation, recovery uses address-targeted `esp_ble_gap_disconnect`, avoiding disconnection of a different peer that reuses a connection ID. A same-peer reconnect can still race validation. Up to three disconnect requests are spaced 15 s apart. This recovery path escalates to global restart only when every remaining link has failed to disconnect for 120 s; a healthy remaining link prevents escalation.

Advertisingの空きがあって停止中なら5秒、開始待ちなら20秒で再試行対象とし、監視側の要求間隔は20秒以上。BLE初期設定が120秒完了しない場合、または接続0台でAdvertising復旧が120秒以上進まない場合は本体再起動の対象とする。端末が接続中ならAdvertisingだけの異常では本体を再起動しない。実際のスマホの再接続開始はアプリ側に依存する。

Missing advertising becomes eligible for retry after 5 s inactive or 20 s pending, with supervisor requests at least 20 s apart. Incomplete BLE setup after 120 s, or stalled advertising recovery for 120 s with zero clients, can restart the ESP32. Advertising failure alone does not restart it with connected clients. Phone reconnection initiation remains app-controlled.

RTCの未初期化領域にmagic・連続自動リセット回数・計画再起動フラグ・理由を保持する。3回の連続自動リセット後、その起動ではタスクwatchdogのpanicと監視タスクの全体再起動を抑止する。端末別復旧は継続する。起動後10分かつ既知の異常なしで次回向け回数をリセットするが、その起動の抑止は解除しない。割り込みwatchdogや別のpanicはこの上限で抑止しない。RTC保持は電源断を越える永続保存ではない。

RTC no-init memory retains magic, consecutive automatic-reset count, planned-restart flag and cause. Three consecutive automatic resets suppress task-watchdog panic and supervisor global restarts for that boot, while peer recovery continues. After ten minutes without a known fault, the next-boot counter is renewed without lifting current suppression. Interrupt-watchdog and unrelated panic resets are not suppressed; RTC data does not survive complete power removal.

`GUARD_BOOT`の`cause`は0=計画理由なし、1=BLEコールバック停止、2=全接続の切断未完了、3=Advertising復旧停止、4=BLE初期設定停止。タスクwatchdogによる再起動は`reset`でも識別する。再起動直前のフラッシュログ保存はbest effortであり、RTCと次回起動記録で補う。新モジュールの実機復旧・誤検知・長時間運用は未検証である。

In `GUARD_BOOT`, `cause` is 0=no planned cause, 1=stalled callback, 2=all disconnects stalled, 3=advertising recovery stalled, 4=BLE setup stalled. Task-watchdog resets are identified by `reset`. Pre-reset flash logging is best effort, supplemented by RTC and next-boot records. Hardware recovery, false-positive behavior and long-duration operation of this module remain unverified.

### 実機で確認する項目 / Hardware acceptance checks

1. 2台→3台を接続し、認証完了後の無操作状態でも切断しないことを確認する。
2. 1台だけ認証を途中で止め、45秒／120秒の復旧が他端末を切断しないことを確認する。
3. テスト用ビルドで通知タスクを停止し、watchdog再起動と次回のリセット理由を確認する。通常配布版には故障注入コマンドを含めない。
4. Advertising開始失敗と切断要求未完了をテスト用ビルドで注入し、再試行・全体復旧条件を確認する。
5. 短時間にwatchdog再起動を3回発生させ、4回目が抑止されること、完全な電源OFFで解除されることを確認する。
6. 回線不良と対象なしの状況で誤切断しないこと、保存ログの取得ができることを確認する。

Test idle authenticated operation with two then three phones; isolate an intentionally stalled handshake; inject notification-task, advertising and disconnect faults in a test-only build; verify reset reasons, three-reset loop protection and full-power-cycle recovery; confirm poor phone connectivity/absence of targets does not trigger false disconnects, and retrieve saved diagnostics. Fault injection is not enabled in normal firmware.

## 9. 操作判別・認証受付・記録負荷の改善 / Request classification, authentication acceptance and logging

`main/pgp-led.c`は[spezifisch/pgpemuのLED判別](https://github.com/spezifisch/pgpemu/blob/master/pgpemu-esp32/main/pgp_led_handler.c)を基にした独立したCモジュール。BSD表示を`main/LICENSE.LED`に保持する。長さを検証した上で、各パターンのRGB下位4bitと消灯・白点滅等の組み合わせを調べる。振動／補間bitは色判定に含めない。緑のみ・黄のみ・青のみの要求に自動応答し、既知の結果・エラー・消灯では押さない。未知の有効パターンは従来どおり押し、`LED_UNKNOWN`で原文を残す。公式プロトコル仕様ではなく観測ベースの判別であるため、新パターンには追加検証が必要。

`main/pgp-led.c` adapts the [spezifisch LED classifier](https://github.com/spezifisch/pgpemu/blob/master/pgpemu-esp32/main/pgp_led_handler.c), with BSD notices in `main/LICENSE.LED`. After bounds checks it examines RGB nibbles and combinations of off/white patterns, excluding vibration/interpolation bits from color. Known green/yellow/blue requests trigger buttons; recognized results/errors/off do not. Unknown valid patterns retain legacy button behavior and detailed `LED_UNKNOWN` logging. This is observation-based, not an official protocol specification.

`queue_notification`／`queue_certificate`はキュー受付の成否を返す。全6段階の認証遷移と最初のCCCD購読時の状態設定は、受付成功時だけ進める。応答用バッファは接続専用のまま。満杯は該当世代の監視へ即時報告し、認証状態の停滞にも期限を設ける（第8節）。認証終点への遷移もキュー受付を示すだけで、無線送達やアプリの接続済み表示とは区別する。Advertising復旧は第8節の周期監視を再利用する。

`queue_notification`/`queue_certificate` return acceptance. All six authentication transitions and the initial CCCD-triggered state update advance only on acceptance. Response buffers remain per-client. Overflow immediately reports a fault for that generation; section 8 supplies authentication deadlines. Even the final state transition denotes queue acceptance, not radio delivery or app connected status. Advertising recovery reuses section 8's periodic supervision.

通常ボタン通知の初回送信ログと既知LEDコマンドの逐次保存をやめ、接続世代ごとのカウンターを短いロック下で更新する。接続中スロットだけ60秒ごとに`TX_HEALTH`、切断時に`CLIENT_SUMMARY`を保存する。認証通知・再試行・異常は詳細を維持する。`TX_HEALTH`の`cc`は認証／ボタンの購読、`busy`は混雑、`q`はキュー残数、`p`は保留、`sent`はボタンAPI受付、`req`は既知要求、`caught/fled/spun`は推定結果、`err/off/unk`は表示エラー／消灯／未知。GPIO2は台数表示のみ。対象がいないだけではwatchdogを発動しない。

First-attempt normal button logs and individual known LED records become counters protected by a short lock. Only active slots emit 60-s `TX_HEALTH`; disconnect emits `CLIENT_SUMMARY`. Certificate/retry/failure detail remains. `cc` = certificate/button subscriptions; `busy` = congestion; `q` = queue depth; `p` = pending; `sent` = button API acceptance; `req` = known requests; `caught/fled/spun` = inferred results; `err/off/unk` = errors/off/unknown. Counters reset with connection/authentication generation. GPIO2 remains connection-count only; no-target inactivity never triggers watchdog recovery.

保存後に256byteを読み戻して比較し、不一致・読み書きエラーで記録を停止する。起動時`DIAG_SCAN`と取得ツールは、識別子なしの非消去領域`non_record`と有効識別子付きCRC不一致`crc_bad`を分ける。2026-10-11回収分は正常1269件・識別子なし736領域・消去43領域・CRC不一致0件だった。識別子なし領域の由来は未確定であり、生ダンプを公開しない。レコード形式と過去ログの互換性は維持する。接続なしの`HEARTBEAT`は600秒へ減らし、新規接続後は60秒以内へ期限を戻す。

Each 256-byte write is read back and compared; mismatch/read/write errors stop recording. Boot `DIAG_SCAN` and export distinguish non-erased missing-magic slots (`non_record`) from recognized records failing CRC (`crc_bad`). The 2026-10-11 dump had 1,269 valid, 736 non-record, 43 erased and zero CRC-bad slots. Non-record data's origin is unconfirmed: raw dumps must not be published. Record format/backward compatibility are unchanged. Idle `HEARTBEAT` interval becomes 600 s, shortened to at most 60 s after a new connection.

### ホストテスト / Host tests

Linux/WSLのPython 3と`cc`で、改修版フォルダーから次を実行する。WindowsネイティブではCコンパイラーを必要とする2クラスはスキップするため、全項目確認にはLinux/WSLを使う。

Run from this project with Python 3 and `cc` on Linux/WSL. The two native-C test classes skip on Windows; use Linux/WSL for full coverage.

```sh
python3 -m unittest discover -s tools -p 'test_*.py' -v
```

本体と同じLED分類Cコードを境界値・切り詰め・ランダム入力で検証し、認証ハンドラーの実コードを暗号処理・キューのモックと組み合わせて受付成功／失敗・接続分離を検証する。暗号の正しさ、BLE配送、スケジューリング、watchdog故障復旧の実機テストの代わりではない。非公開ログを`PGP_LED_REPLAY`に指定した実行では103コマンドの分類も再生し、計18テストを通過した。通常実行は非公開ログの項目をスキップする。

Tests compile the production LED classifier with boundary/truncation/random inputs and the actual authentication handler control flow with mocked crypto/queue operations, checking accepted/rejected transitions and peer isolation. They do not validate cryptography, BLE delivery, scheduling or hardware watchdog recovery. An optional private-log replay via `PGP_LED_REPLAY` classified 103 commands; all 18 tests passed with that input. Normal runs skip that private fixture.

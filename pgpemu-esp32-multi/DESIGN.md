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
| LEDコマンド / LED commands | アプリから操作要求が来ているか。捕獲成功を分類しない / Incoming app activity, not capture-success classification |
| `NOTIFY_TRY`, `NOTIFY_SENT`, `NOTIFY_RETRY` | 送信開始・API受理・即時失敗 / Start, API acceptance, immediate failure |
| `NOTIFY_DROP`, `NOTIFY_QUEUE_FULL`, `NOTIFY_ASYNC_FAIL` | 期限・世代・購読・満杯・非同期失敗 / Expiry, generation, subscriptions, overflow, async failure |
| `CONGEST`, `TX_HEALTH` | 接続別の混雑・残キュー・保留 / Per-client congestion, queue and pending item |
| `HEARTBEAT` | 30秒ごとの接続数、Advertising、メモリ、イベント数、欠落数 / 30-s client, advertising, memory, event and loss summary |

回収は`tools/download-diagnostics.py --port COM3`をESP-IDFのPythonで実行する。診断領域だけを読み、テキスト化する。操作前後のリセットがあるため、不要な再起動を重ねず早めに回収する。取得前の`erase-flash`は禁止。単体テストは`python -m unittest discover -s tools -p test_diagnostics.py`。

Run `python tools/download-diagnostics.py --port COM3` in the ESP-IDF environment to read only the diagnostic partition and decode it. Retrieval resets the board; retrieve promptly and avoid unnecessary intervening reboots. Do not run `erase-flash` first. Decoder tests: `python -m unittest discover -s tools -p test_diagnostics.py`.

## 7. 公開境界と残る制約 / Publication boundary and limitations

実機用`main/secrets.c`、抽出JSON、実機用ビルド成果物、診断のエクスポートは公開しない。診断にはMAC、Device Key、Blob、session key、認証ペイロードを記録しない。ただし通常のシリアルログには機器情報や認証データが出る場合があるため、そのまま公開しない。

Do not publish real `main/secrets.c`, extracted JSON, device-specific build artifacts, or diagnostic exports. Persistent records exclude MAC addresses, Device Key, Blob, session keys and authentication payloads. Ordinary serial output may contain device/authentication information and must be reviewed before sharing.

本変更は停止の原因を実機で確定したものではない。通知期限切れや非同期送信失敗は接続・認証のやり直しを必要とする場合がある。Advertising開始APIの即時失敗に対する周期的な自動復旧はない。世代番号の再確認と送信API呼び出しは一体の原子的操作ではない。フラッシュ記録によるBLEタイミングへの影響も実機評価が必要。これらは今後のログ解析と検証対象とする。

The root cause of the stall has not been established on hardware. Expired authentication notifications or asynchronous failures may require reconnecting. There is no periodic recovery for an immediate advertising-start API failure. Generation validation and the subsequent send API call are not one atomic operation. Flash logging's impact on BLE timing also needs hardware evaluation. These remain targets for field logs and further testing.

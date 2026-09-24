# PGP-Badge ESP32 複数端末対応版

[English README](README.md)

このディレクトリは、元の [PGP-Badge](https://github.com/bentomo/PGP-Badge) のファームウェアを、ESP32-WROOM-32で最大3台のPokémon GO端末と同時にBLE接続できるよう改修したものです。ESP-IDF v4.4.8とBluedroidを使用します。元プロジェクトはBSD 2-Clauseライセンスです。再配布時はリポジトリの[LICENSE](../LICENSE)と著作権表示を残してください。

ESP32向けのビルドは成功しています。ただし、iPhoneとAndroidを含む複数端末での同時接続・認証・再接続は、まだ実機で検証していません。

## 謝辞

Pokémon GO PlusのリバースエンジニアリングとBSD 2-Clauseライセンスの元コードを公開された[Yohanes Nugroho氏](https://github.com/yohanes)、PGP-BadgeのハードウェアとESP32ファームウェアを公開された[bentomo氏](https://github.com/bentomo/PGP-Badge)、および元リポジトリの貢献者の皆様に感謝します。この派生版は皆様の成果を基にしています。Pokémon GOやESP-IDFの公式プロジェクトではありません。

## 実機用データと公開用ファイル

`main/secrets.c`には実機固有のMAC、DEVICE_KEY、BLOBを入れます。このファイルはGitの管理対象外です。実機用の`secrets.c`、それを組み込んだファームウェアのバイナリ、`build`ディレクトリを公開リポジトリへ追加しないでください。

新たにローカルビルドを準備するときは、`main/secrets.example.c`を`main/secrets.c`へコピーし、所有する機器の値で仮の値を置き換えて、`#error`行を削除してください。すでにローカルに実機用`main/secrets.c`がある場合は、サンプルで上書きしないでください。

## ビルド

PowerShellで実行します。

```powershell
& "C:\esp\v4.4.8\esp-idf\export.ps1" # ESP-IDFの場所が異なる場合は変更
cd pgpemu-esp32-multi
idf.py fullclean
idf.py build
```

`CONFIG_BTDM_CTRL_BLE_MAX_CONN`は3以上が必要です。`sdkconfig.defaults`では3を指定しています。ビルドしたバイナリには実機用データが含まれるため、GitHubのReleaseにもアップロードしないでください。

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

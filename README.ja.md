[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [繁體中文](README.zh-TW.md)

# TempHumi Monitor 温湿度モニタ

STM32F103C8T6 を用いた温湿度収集システムです。Qt デスクトップアプリ、チャネル別の明示的なデータソース、生データ記録、Windows のポータブル版とインストーラ版を備えます。

デスクトップ **v010.18.1**、MCU ファームウェア **v010.17.1**。動作確認、シミュレーション、独立した物理精度の評価は区別しています。

![TempHumi v010.18.1: REAL temperature / MCU SIM humidity](docs/release/desktop-v010.18.1.png)

## 主な機能

- 実温度の収集と3種類の**暫定的な** MCU 温度補正。補正前の平均値と公開値を両方保存します。
- 温度と湿度の REAL/SIM を MCU で明示的に選択できます。実温度と MCU 模擬湿度の混在に対応し、故障時の自動 SIM 切替は行いません。
- TH3 のシリアル／追記ファイル／REPLAY 入力、独立した縦軸、点・イベントの確認、SHA-256 検証付き全セッション出力。
- 表示の一時停止中も記録を継続。End Session で記録を確定してポートを解放します。F1 はオフラインヘルプ、About はアプリと実際に応答した MCU を区別します。
- 自動再接続と確認済み設定の復元は初期状態で有効です。5秒間隔・最大12回とし、新しい機器識別・ファームウェア・読戻し結果を確認してから設定を変更します。

## ハードウェアと動作環境

STM32F103C8T6、ADS1115、NTC/U6 温度調整回路、CHS1101/TLC556/OPA330 湿度回路、OLED、CH340 USB-UART を使用します。検証済み Windows 配布物は Windows10/11 x64 と適切な CH340 ドライバが必要です。書込みは ST-Link/SWD を使用します。回路図は設計資料であり、実装基板の導通証明ではありません。

[Hardware evidence](docs/hardware/FINAL_TECHNICAL_AUDIT.md) · [Architecture](docs/architecture/OVERVIEW.md)

## ソフトウェア構成

HAL／プラットフォーム → BSP → Driver → 純粋な Algorithm → Application／ソース選択 → Protocol。OLED と UART は同じ公開値を使い、Qt は再校正しません。MCU の起動時は REAL/REAL です。

## ビルドとファームウェア書込み

検証環境は Qt5.14.2/MinGW7.3x64、STM32CubeIDE1.19.0/Arm GCC13.3、Python3、STM32CubeProgrammer です。.project/.cproject を読み込むか、以下のコマンドを使用します。書込み前に対象、電源、イメージ由来、収集停止を確認し、Flash 全体を退避してください。BIN の開始番地は0x08000000。Verify 後に読戻しをバイト単位で比較します。64KiB Flash／20KiB RAM の範囲に収まります。

```powershell
python tools/build_desktop.py --configuration both --label verification --build-root build_verify/qt --evidence build_verify/qt_logs
python tools/build_mcu.py --build-root build_verify/mcu --evidence build_verify/mcu_logs
python tools/run_regressions.py --build-root build_verify/tests --evidence build_verify/test_logs --desktop-objects build_verify/qt/qt_debug/debug
```

[Build / flash instructions](docs/development/BUILD.md)

## Windows ポータブル版と Setup

Release から ZIP または署名なし Setup を取得します。ZIP はフォルダ全体を展開して TempHumi.exe を起動してください。Setup は書込み可能な場所に同じ検証済みランタイムを配置し、スタートメニューと任意のデスクトップショートカットを作成します。自動接続や MCU 書込みは行いません。アンインストールは利用者のデータと設定を保持します。Qt DLL／プラグインも必ず一緒に配置してください。

[Release](https://github.com/HanamiGekka/STM32-TempHumi-Monitor/releases/tag/v010.18.1) · [Desktop guide](docs/development/DESKTOP.md)

## データ収集とプロトコル

現在のポートを確認し、115200/8N1／フロー制御なしを使用します。起動直後は SERIAL を準備するだけで接続しません。保存先を選択して開始してください。ソース／補正変更は Apply と MCU 応答で確定します。TH3 は生コード、単位、有効性、エラー、各ソース、世代を含む23フィールドです。表示窓は5–1200秒、最大24000点。生データ／CSV 記録は表示範囲の切詰めから独立しています。

[TH3](docs/protocol/TH2_TH3.md) · [UART commands](docs/protocol/UART_COMMANDS.md)

## 試験と検証

MCU と Qt の Debug/Release のフルビルドは、いずれもエラー・警告なしで完了しました。ホスト回帰22スイート、表示倍率100%/150%で各94項目のネイティブ GUI 試験、ポータブル版120項目、Bundle102項目に合格しています。ポータブル版とインストール版では、それぞれホスト側の記録時間とソース時刻の前進量がともに3600秒以上となる実機試験を実施しました。時間・フレーム数・保存した異常は検証報告に記載しています。実際のインストール、ヘルプ、記録、シリアル通信、アンインストール時のデータ保持も確認済みです。

提供された10組の温度比較では、Raw MAE0.754℃／RMSE1.020725℃、補正後 MAE0.522℃／RMSE0.720777℃、補正後最大絶対誤差1.53℃、±1℃以内8/10でした。全測定範囲の計量精度を保証する結果ではありません。

[Validation](docs/release/VALIDATION.md)

## 既知の制約

- 実湿度は **PARTIAL / NOT VALIDATED**。模擬湿度は実 RH 精度の証拠になりません。
- PWM 波形・周波数・デューティ比は **NOT TESTED**。V_CHS1101 のほぼ0Vという観測とバッファ／ADC の直流値には未解決の矛盾があります。
- USB 異常の物理原因は **UNRESOLVED**。復旧ログだけでケーブル、電源、はんだ接合の原因は断定しません。
- 温度補正は暫定です。独立した全範囲精度、新たな OLED 光学測定、別の実 Windows PC での検証は主張しません。

## 配布物とソースの対応

デスクトップ v010.18.1 は審査済み公開コミットからビルドし、MCU v010.17.1 は検証済みバイト列を保持します。再ビルドはそのコミットと時刻を埋め込むため、同一版の正式置換とは扱いません。独立した公開履歴から、製品入力ハッシュ・凍結ソース・配布物 SHA-256 の対応を追跡できます。生セッション、機器識別子、非公開履歴、基板写真、アカウントを含む CAD データベースは公開しません。

[Source / asset identity](docs/release/SOURCE_IDENTITY.json)

## ライセンスと貢献

プロジェクト独自のソース／文書は **GPL-3.0-or-later**。QCustomPlot は Emanuel Eichhammer 氏によるもので、Qt/ST/Arm/MinGW は各上流ライセンスを保持します。リポジトリ所有者が主な開発者です。他のチームメンバーの貢献にも謝意を表し、メンバーは公開に同意しています。対応する Qt ソースとランタイムの告知を Release に添付します。

[LICENSE](LICENSE) · [NOTICE](NOTICE.md) · [Dependency terms](docs/release/THIRD_PARTY_NOTICES.md)

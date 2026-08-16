# M5StickC Plus + Wi-SUN HAT B-Route Power Monitor

M5StickC Plus と Wi-SUN HAT（ROHM BP35A1 搭載）を使用して、家庭のスマートメーター（Bルート）からリアルタイムに電力データを取得・可視化するスマート電力モニターシステムです。

単相3線式の特性を活かし、**全体瞬時電力（W）** に加えて **R相（W/A）・T相（W/A）** の相別電力・電流をリアルタイム表示し、家庭内の家電負荷状況（簡易ディスアグリゲーション）やブレーカー過負荷を監視できます。

---

## 🌟 主な機能・特徴

1. **洗練された M5StickC Plus 液晶表示**
   - **全体瞬時電力（特大 W 表示）** と **R相 / T相（上下分割 W 表示）** の並列レイアウト。
   - 契約アンペア（40Aなど）に近づいた際の **4段階動的カラー変化（緑 ➔ 黄 ➔ 橙 ➔ 赤）**、警告メッセージバッジ表示、画面赤点滅 & 警告ブザー。
   - **画面180度反転**（BtnB 側面ボタン）。

2. **ボタン操作によるポップアップ表示（BtnA 正面M5ボタン）**
   - **左上**: 日本標準時 日付・曜日（例: `08/15 (Sat)`）
   - **右上**: 正確な現在時刻（秒単位リアルタイム更新）
   - **中央特大**: **今月の積算電力量（kWh）** ＆ 検針期間（例: `Date range: 7/23 - 8/22`）
   - **下部**: Web ダッシュボードのアクセス用 URL

3. **検針期間（毎月23日〜翌月22日）の積算電力量の自動計算**
   - カレンダー日（JST）から検針サイクルを自動判定。
   - ESP32 の不揮発性フラッシュメモリ（NVS / Preferences）にベース電力量を自動保存するため、電源OFFやリセット後も今月の積算量が正確に継続されます。

4. **内蔵 Web ダッシュボード（Grafana 風 2軸グラフ ＆ 統計テーブル）**
   - ブラウザから M5StickC の IP アドレスにアクセスするだけで、外部サーバー不要で稼働。
   - **左軸（電流 A: R相 / T相）** ＆ **右軸（電力 W: 全体電力）** の 2軸時系列グラフ（0:00〜24:00、10分刻み 144スロット）。
   - **Grafana 風統計サマリーテーブル**（Min / Avg / Max / Last を自動算出）。
   - JSON API エンドポイント（`/api/power`, `/api/history`）。

5. **24時間365日安定稼働のための堅牢性・高速化設計**
   - **スマートメーターキャッシュ（ファストブート）**: 初回接続時にスマートメーターのチャンネル・PAN ID・IPv6を NVS に保存し、次回起動時のスキャン（約30秒）をスキップして **3〜5秒で高速接続**。
   - **PANA再認証保護 (`EVENT 29`)**: 1日1回の暗号鍵更新時に ECHONET Lite 要求を自動一時停止し、セッション切断を防止。
   - **通信途絶ウォッチドッグ**: 応答途絶時に自動で `SKTERM` ➔ 再スキャン・再接続を実行。
   - **`SKSREG SA1 0` 設定**: 平文ICMPパケットを破棄し暗号化セッションの信頼性を担保。

---

## 🛠 ハードウェア仕様・ピン配置

- **本体**: M5StickC Plus (ESP32-PICO-D4)
- **Wi-SUN モジュール**: ROHM BP35A1（rin-ofumi製 Wi-SUN HAT Rev0.1）
- **UART ピンアサイン**:
  - **TX (M5StickC側)**: `GPIO0`
  - **RX (M5StickC側)**: `GPIO36`
  - **ボーレート**: `115200 bps`

---

## 🚀 セットアップとビルド手順

### 1. 必要環境
- [Visual Studio Code](https://code.visualstudio.com/)
- [PlatformIO IDE 拡張機能](https://platformio.org/)

### 2. 設定ファイルの作成
リポジトリをクローン後、`src/config.h.example` をコピーして `src/config.h` を作成します。

```bash
cp src/config.h.example src/config.h
```

`src/config.h` を開き、電力会社から発行された **Bルート認証ID（32桁）**、**パスワード（12桁）**、およびご自宅の **Wi-Fi SSID / パスワード** を入力します。

```cpp
// src/config.h
#define BROUTE_ID       "00000099021800000000000001D9F614"  // 32桁
#define BROUTE_PASSWORD "BHDJC7DI6U4B"                     // 12桁

#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define AMPERE_LIMIT_WATT   4000  // ご契約アンペア (40Aなら4000)
```

### 3. ビルド ＆ 書き込み
1. M5StickC Plus を USB ケーブルで PC に接続します。
2. VS Code 下部バーの **PlatformIO: Upload (`→`)** ボタンをクリックします。
3. 自動的に依存ライブラリ（`M5Unified`, `ESPAsyncWebServer`, `ArduinoJson` 等）がダウンロードされ、コンパイル・書き込みが完了します。

---

## 📱 操作方法

- **正面 M5ボタン (BtnA)**:
  - 手動データリフレッシュ ＆ **時計・今月積算電力量・Web URL のポップアップ表示（4秒間）**
- **側面ボタン (BtnB)**:
  - **画面の180度上下反転**（電源コードの向きに合わせて切り替え可能）

---

## 📄 ライセンス

MIT License

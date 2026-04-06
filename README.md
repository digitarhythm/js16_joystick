[日本語](#日本語) | [English](#english)

---

<a name="日本語"></a>

# QMK Analog Stick Library

アナログジョイスティックを QMK/Vial ファームウェアでマウスカーソル操作に使用するためのライブラリです。K-SILVER JS16 (TMR) / JH16 (Hall Effect) 等のアナログ出力ジョイスティックに対応しています。

## Features

- **サブピクセル処理**: x1000 スケールの内部精度で、速度 1.0 未満でも滑らかなカーソル移動
- **ベクトル合成**: X/Y 軸を合成ベクトルとして処理し、斜め方向も均一な速度で移動
- **円形デッドゾーン**: 合成ベクトルの大きさで判定。全方向均一なデッドゾーン
- **二乗加速**: スティックの傾き量の二乗に比例した加速度。傾け始めはゆっくり、大きく倒すほど速く加速
- **比例減速**: スティックを戻すと、戻した量に応じて速度が比例的に減速。現在の傾き量に対応した速度上限へ向かって指数的に収束
- **移動平均フィルタ**: ADC ノイズの除去
- **起動時キャリブレーション**: TMR センサーのウォームアップ待機と中心値の自動取得
- **非対称レンジ補正**: 中心値が ADC レンジの中央にない場合でも方向ごとに正規化
- **ボタン対応**: SW ピンによるマウスクリック（GPIO 直結、オプション）
- **全パラメータカスタマイズ可能**: `config.h` の `#define` で上書き

## Requirements

- **MCU**: RP2040 (RP2040-Zero 等)
- **QMK Firmware** (Vial 対応版含む)
- **ChibiOS** (RP2040 の ADC ドライバを使用)

## Files

| File | Description |
|---|---|
| `qmk_analog_stick.h` | ヘッダファイル (デフォルトパラメータ定義 + API 宣言) |
| `qmk_analog_stick.c` | 実装ファイル |
| `halconf.h` | ChibiOS HAL 設定 (ADC 有効化) |
| `mcuconf.h` | ChibiOS MCU 設定 (RP2040 ADC ドライバ有効化) |

## Quick Start

### 1. ファイルの配置

`qmk_analog_stick.h`、`qmk_analog_stick.c`、`halconf.h`、`mcuconf.h` をキーボードディレクトリにコピーします。既に `halconf.h` や `mcuconf.h` が存在する場合は、手順 2・3 の内容を既存ファイルに追記してください。

```
keyboards/your_keyboard/
  ├── qmk_analog_stick.h
  ├── qmk_analog_stick.c
  ├── halconf.h
  ├── mcuconf.h
  └── keymaps/default/
      ├── config.h
      ├── keymap.c
      └── rules.mk
```

### 2. halconf.h

```c
#pragma once

#define HAL_USE_ADC TRUE

#include_next <halconf.h>
```

### 3. mcuconf.h

```c
#pragma once

#include_next <mcuconf.h>

#undef RP_ADC_USE_ADC1
#define RP_ADC_USE_ADC1 TRUE
```

### 4. config.h

最低限、ジョイスティックの接続ピンを定義します。

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29

// ボタン機能を使う場合（オプション）
// #define JOYSTICK_SW_PIN GP13
```

### 5. keymap.c

```c
#include QMK_KEYBOARD_H
#include "qmk_analog_stick.h"

// ... キーマップ定義 ...

void keyboard_post_init_user(void) {
    analog_stick_init();
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    return analog_stick_update(mouse_report);
}
```

### 6. rules.mk

```makefile
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom
SRC += analog.c qmk_analog_stick.c
```

## API Reference

### `void analog_stick_init(void)`

ジョイスティックを初期化します。`keyboard_post_init_user()` 内で呼び出してください。

**処理内容:**

1. `JOYSTICK_WARMUP_MS` ミリ秒待機 (TMR センサー安定化)
2. 64 回のサンプリングで X/Y 軸の中心値をキャリブレーション
3. 移動平均バッファを中心値で初期化
4. `JOYSTICK_SW_PIN` 定義時、ボタンピンを入力プルアップに設定
5. デバッグ有効時、キャリブレーション結果をコンソール出力

**注意:** 初期化中はスティックに触れないでください。触れた状態の値が中心値として記録されます。

---

### `report_mouse_t analog_stick_update(report_mouse_t mouse_report)`

毎スキャンサイクルでマウスレポートを更新します。`pointing_device_task_user()` 内で呼び出してください。

**引数:**

| Name | Type | Description |
|---|---|---|
| `mouse_report` | `report_mouse_t` | 現在のマウスレポート |

**戻り値:**

更新されたマウスレポート (`mouse_report.x`、`mouse_report.y`、`JOYSTICK_SW_PIN` 定義時は `mouse_report.buttons` が設定される)

**処理内容:**

1. ADC 読み取り + 移動平均フィルタ
2. 各軸を -1000〜+1000 に正規化（非対称レンジ補正）
3. X/Y の合成ベクトル（magnitude）を計算
4. 円形デッドゾーン判定
5. 現在の傾き量から速度上限 (speed_limit) を計算（二乗カーブ）
6. `current_speed < speed_limit` なら加速、`current_speed > speed_limit` なら比例減速
7. 合成速度を X/Y 方向比率で分配
8. サブピクセル蓄積 + 整数ピクセル変換
9. ボタン状態読み取り (`JOYSTICK_SW_PIN` 定義時)

## Architecture

### 速度決定の流れ

```
スティック傾き
     ↓
各軸を正規化 (-1000〜+1000)
     ↓
合成ベクトル magnitude = √(x² + y²)
     ↓
円形デッドゾーン判定 (magnitude > deadzone?)
     ↓  YES
デッドゾーン差し引き → adjusted_magnitude (0〜1000)
     ↓
速度上限: speed_limit = adjusted_magnitude² × MAX_SPEED / 1000000
     ↓
current_speed < speed_limit?  → 加速: current_speed += adjusted_magnitude² × ACCEL_RATE / 1000000
current_speed > speed_limit?  → 減速: current_speed -= (current_speed - speed_limit) × DECEL_RATE / 100
     ↓
方向分配: speed_x = current_speed × norm_x / magnitude
          speed_y = current_speed × norm_y / magnitude
     ↓
サブピクセル蓄積 → mouse_report.x, mouse_report.y
```

### 加速の仕組み

スティックの傾き量の**二乗**が毎サイクルの加速度になります。倒し続けると速度が蓄積されていきます。

```
加速度/サイクル = (adjusted_magnitude / 1000)² × ACCEL_RATE
速度上限 = (adjusted_magnitude / 1000)² × MAX_SPEED
```

- **少し倒す**: 加速度が小さい → ゆっくり加速 → 精密操作
- **大きく倒す**: 加速度が大きい → 速く加速 → 素早い移動
- **どの傾きでも** 傾き量に対応した速度上限まで到達可能
- **スティックをデッドゾーンまで戻す**: 速度が即座にリセット

| 傾き | 加速度/cycle | 速度上限 (MAX_SPEED=8000) |
|---|---|---|
| 30% | 0.00144 | 720 (0.72 px/cycle) |
| 50% | 0.004 | 2000 (2.0 px/cycle) |
| 70% | 0.00784 | 3920 (3.92 px/cycle) |
| 100% | 0.016 | 8000 (8.0 px/cycle) |

### 減速の仕組み

スティックを戻すと `speed_limit` が下がり、`current_speed` がそれを超えた状態になります。その差分の `DECEL_RATE%` ずつ毎サイクル削減されます（指数的減衰）。

```
減速量/サイクル = (current_speed - speed_limit) × DECEL_RATE / 100
```

- **ちょっと戻す**: speed_limit が少し下がる → 緩やかな減速
- **大きく戻す**: speed_limit が大きく下がる → 急減速
- **デッドゾーンまで戻す**: 最後の方向でゆっくり停止

**DECEL_RATE=50（デフォルト）での減速例:**

全倒し（speed=8000）から 50% 戻し（speed_limit=2000）にした場合:

| フレーム | 速度 |
|---|---|
| 0 | 8000 |
| 1 | 5000 |
| 2 | 3500 |
| 3 | 2750 |
| 5 | 2100 |
| 8 | ≈2000 |

約 80ms（8フレーム @ 100Hz）で目標速度に到達します。

### 円形デッドゾーン

デッドゾーンは X/Y 軸の合成ベクトルの大きさで判定します。

```
変更前（四角形）     本ライブラリ（円形）
+---+---+              .---.
|   |   |             /     \
+---+---+            |   o   |
|   |   |             \     /
+---+---+              '---'
```

四角形デッドゾーンでは斜め方向で片方の軸だけ先に反応し、4方向に引っ張られる現象が発生します。円形デッドゾーンは全方向均一なデッドゾーンを提供します。

### ベクトル合成

X/Y 軸を独立に処理すると、斜め方向で速度が不均一になります。本ライブラリでは合成ベクトルの大きさに対して加速度を計算し、方向比率で X/Y に分配します。

```
magnitude = √(norm_x² + norm_y²)
speed_x = current_speed × norm_x / magnitude
speed_y = current_speed × norm_y / magnitude
```

## Parameter Reference

すべてのパラメータは `config.h` で `#define` することで上書きできます。定義しない場合はデフォルト値が使用されます。

### Required Parameters

| Parameter | Description | Example |
|---|---|---|
| `JOYSTICK_X_PIN` | X 軸の ADC ピン | `GP28` |
| `JOYSTICK_Y_PIN` | Y 軸の ADC ピン | `GP29` |

### Button Parameters

`JOYSTICK_SW_PIN` を定義するとボタン機能が有効になります。未定義の場合、ボタン関連の処理は一切含まれません。

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_SW_PIN` | (未定義) | SW ピンの GPIO。定義するとボタン機能有効 |
| `JOYSTICK_SW_BUTTON` | `MOUSE_BTN1` | ボタンに割り当てるマウスボタン |

**使用可能なマウスボタン定数:**

| Constant | Description |
|---|---|
| `MOUSE_BTN1` | 左クリック (デフォルト) |
| `MOUSE_BTN2` | 右クリック |
| `MOUSE_BTN3` | 中クリック (ホイールクリック) |
| `MOUSE_BTN4` | 戻る |
| `MOUSE_BTN5` | 進む |

**設定例:**

```c
// 左クリック（デフォルト）
#define JOYSTICK_SW_PIN GP13

// 右クリックに変更
#define JOYSTICK_SW_PIN GP13
#define JOYSTICK_SW_BUTTON MOUSE_BTN2
```

**配線:**

ジョイスティックの SW ピンは押下時に内部 GND に接続される（アクティブ LOW）ため、GPIO に直接接続するだけで動作します。外部プルアップ抵抗は不要です（MCU 内部プルアップを使用）。

```
Joystick SW -----> GPIOピン（内部プルアップ有効）
```

> **注意:** ジョイスティックの SW ピンはキーマトリクスには直接接続できません。SW は押下時に内部 GND に短絡する 1 ピン出力のため、マトリクスの ROW-COL 間接続として機能しません。GPIO 直結で使用してください。

### Speed / Acceleration Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_MAX_SPEED` | `8000` | 最大速度 (x1000 スケール、8000 = 8.0 px/cycle) |
| `JOYSTICK_ACCEL_RATE` | `16` | 加速度係数。大きいほど速く加速する |
| `JOYSTICK_DECEL_RATE` | `50` | 減速率 (%)。スティックを戻したとき速度上限との差のこの割合ずつ減速する |

- `MAX_SPEED`: 全倒し時の速度上限。傾き量に応じた速度上限は `(傾き%)² × MAX_SPEED` になります
- `ACCEL_RATE`: 傾き量の二乗にこの値を掛けたものが毎サイクルの加速度になります。全倒しで約5秒で速度上限に到達します
- `DECEL_RATE`: スティックを戻したとき、目標速度（speed_limit）との差のこの割合ずつ毎サイクル減速します。大きいほど戻し量への追従が素早くなります

**ACCEL_RATE の目安:**

| ACCEL_RATE | 全倒し時の速度上限到達時間 |
|---|---|
| `8` | 約10秒 |
| `16` | 約5秒 (デフォルト) |
| `32` | 約2.5秒 |
| `64` | 約1.25秒 |

**DECEL_RATE の目安（全倒し→50%戻しの場合）:**

| DECEL_RATE | 目標速度到達フレーム数 (100Hz) |
|---|---|
| `30` | 約15フレーム (150ms) |
| `50` | 約8フレーム (80ms)（デフォルト） |
| `80` | 約4フレーム (40ms) |

### ADC Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_ADC_MIN` | `0` | ADC の実測最小値（X/Y 共通デフォルト） |
| `JOYSTICK_ADC_MAX` | `1023` | ADC の実測最大値（X/Y 共通デフォルト） |
| `JOYSTICK_ADC_X_MIN` | `JOYSTICK_ADC_MIN` | X 軸の実測最小値 |
| `JOYSTICK_ADC_X_MAX` | `JOYSTICK_ADC_MAX` | X 軸の実測最大値 |
| `JOYSTICK_ADC_Y_MIN` | `JOYSTICK_ADC_MIN` | Y 軸の実測最小値 |
| `JOYSTICK_ADC_Y_MAX` | `JOYSTICK_ADC_MAX` | Y 軸の実測最大値 |

X 軸と Y 軸で出力レンジが異なるジョイスティック（K-SILVER JH16 等）では、軸ごとに個別設定できます。`JOYSTICK_ADC_X_MIN` 等を定義しない場合は `JOYSTICK_ADC_MIN` / `JOYSTICK_ADC_MAX` が両軸に使用されます。

```c
// X/Y で出力レンジが違う場合の例
#define JOYSTICK_ADC_Y_MAX  784   // Y軸の最大値が小さい場合
```

### Filter Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEADZONE` | `15` | 中心付近のデッドゾーン (ADC 値) |
| `JOYSTICK_SMOOTHING` | `4` | 移動平均のサンプル数 |

- `DEADZONE`: 円形デッドゾーンの半径（ADC 値単位）。合成ベクトルの大きさがこの値以下の場合、カーソルは動きません。小さいほど敏感、大きいほど安定
- `SMOOTHING`: 移動平均のウィンドウサイズ。大きいほど滑らかだが応答が遅延

### Startup Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_WARMUP_MS` | `3000` | TMR センサー安定待ち時間 (ms) |

TMR センサーは電源投入直後に出力が安定しないため、キャリブレーション前に待機します。

### Debug Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEBUG` | `1` | デバッグ出力 (`1`: 有効, `0`: 無効) |

有効時、コンソール (`keyboard.json` で `"console": true`) に約 1 秒ごとに以下の情報を出力します:

```
Xr=450 cx=449 dx=0 | Yr=520 cy=519 dy=0 | spd=0
```

| Field | Description |
|---|---|
| `Xr` / `Yr` | スムージング後の ADC raw 値 |
| `cx` / `cy` | キャリブレーションされた中心値 |
| `dx` / `dy` | 出力されたマウス移動量 (ピクセル) |
| `spd` | 現在の速度 (x1000 スケール) |

デバッグ出力を確認するには:
- **QMK Toolbox**: 接続するとコンソール出力が表示される
- **qmk console**: ターミナルで `qmk console` を実行

## Configuration Examples

### 精密操作重視 (CAD / デザイン作業向け)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_DEADZONE      60
#define JOYSTICK_MAX_SPEED   3000   // 3.0
#define JOYSTICK_ACCEL_RATE     4   // 全倒しで約20秒でMAX
#define JOYSTICK_DECEL_RATE    30   // ゆっくり減速
```

### 高速操作重視 (ブラウジング / ゲーム向け)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_DEADZONE      20
#define JOYSTICK_MAX_SPEED  15000   // 15.0
#define JOYSTICK_ACCEL_RATE    64   // 全倒しで約1.25秒でMAX
#define JOYSTICK_DECEL_RATE    80   // 素早く減速
```

### ボタン付き（左クリック）

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_SW_PIN      GP13
```

### ボタン付き（右クリック）

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_SW_PIN      GP13
#define JOYSTICK_SW_BUTTON   MOUSE_BTN2
```

### デフォルト設定のまま使用（ボタンなし）

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29
```

## Hardware Notes

### 対応ジョイスティック

本ライブラリは ADC でアナログ電圧を読み取る方式のジョイスティックに広く対応しています。動作確認済みのモデル:

- **K-SILVER JS16** (TMR 方式)
- **K-SILVER JH16** (Hall Effect 方式)

### K-SILVER JS16 / JH16

- **センシング方式**: TMR (トンネル磁気抵抗効果) / Hall Effect
- **動作電圧**: 1.8V 〜 3.3V
- **消費電力**: 約 210 〜 215 uA
- **耐久性**: 約 500 万回転
- **ピン**: VCC, GND, X 出力, Y 出力, SW (ボタン)

### RP2040 ADC ピン

RP2040 で ADC として使用可能なピンは以下の 4 つです:

| Pin | ADC Channel |
|---|---|
| GP26 | ADC0 |
| GP27 | ADC1 |
| GP28 | ADC2 |
| GP29 | ADC3 |

**注意**: 一部のボードでは GP29 が VSYS 電圧計測用に使用されている場合があります。その場合は GP26/GP27/GP28 を使用してください。

### 配線例

```
Joystick      RP2040-Zero
----------    -----------
VCC    ----->  3.3V
GND    ----->  GND
X out  ----->  GP28  (ADC ピン)
Y out  ----->  GP29  (ADC ピン)
SW     ----->  GP13  (任意の GPIO、オプション)
```

### EC12 エンコーダーフットプリント流用時の配線

K-SILVER JS16/JH16 は EC12/EC11 ロータリーエンコーダーのフットプリントに実装できますが、ピンの役割が異なるため配線に注意が必要です。

```
EC12 ピン      Joystick ピン  接続先
---------      ------------   ------
Encoder A  --> X output   --> ADC ピン (GP28 等)
Encoder GND -> GND        --> GND
Encoder B  --> Y output   --> ADC ピン (GP29 等)
Switch 1   --> VCC        --> 3.3V 電源ライン (※)
Switch 2   --> SW         --> GPIO ピン (※)
```

> **(※) 注意:** EC12 のスイッチピンは通常キーマトリクスの ROW/COL に接続されています。ジョイスティックの VCC は常時 3.3V 給電が必要なため、Switch 1 のパッドが 3.3V 電源ラインに接続されていることを確認してください。Switch 2 (SW) はマトリクスではなく GPIO に直結する必要があります。

## Troubleshooting

### カーソルが勝手に動く

- `JOYSTICK_DEADZONE` を大きくしてください (例: `80`)
- `JOYSTICK_WARMUP_MS` を大きくしてキャリブレーション精度を上げてください (例: `5000`)
- 起動時にスティックに触れないでください

### カーソルが動かない

- `halconf.h` で `HAL_USE_ADC TRUE` が定義されているか確認
- `mcuconf.h` で `RP_ADC_USE_ADC1 TRUE` が定義されているか確認
- `rules.mk` に `SRC += analog.c` が含まれているか確認
- ジョイスティックの VCC が 3.3V に接続されているか確認
- `JOYSTICK_DEBUG 1` でコンソール出力を確認し、ADC 値が変化するか確認

### 方向によって速度が違う

- TMR センサーの中心出力が ADC レンジの中央にない場合に発生します
- 本ライブラリはベクトル合成と方向ごとの自動スケーリングで対処しています
- 極端に非対称な場合は `JOYSTICK_ADC_MIN` / `JOYSTICK_ADC_MAX` を実測値に合わせてください

### 4 方向にしか動かない / 4 方向に引っ張られる

- 本ライブラリはベクトル合成と円形デッドゾーンで対策済みです
- それでも発生する場合は `JOYSTICK_ACCEL_RATE` を小さくして最大速度を抑えてください
- `JOYSTICK_SMOOTHING` を大きくすると入力が滑らかになります

### ボタンが反応しない

- `config.h` で `JOYSTICK_SW_PIN` が定義されているか確認
- SW ピンが GPIO に直接接続されているか確認（キーマトリクス経由では動作しません）
- テスターで SW ピンと GND 間を計測し、押下時に導通するか確認

### ボタンが押しっぱなしになる

- SW ピンが GND にショートしていないか確認
- 配線が正しいか確認（SW ピンはアクティブ LOW: 押すと GND に接続）

## License

MIT

---

<a name="english"></a>

# QMK Analog Stick Library

A library for using analog joysticks as mouse cursor controllers in QMK/Vial firmware. Compatible with analog output joysticks such as the K-SILVER JS16 (TMR) and JH16 (Hall Effect).

## Features

- **Sub-pixel processing**: Internal x1000 scale precision for smooth cursor movement even at speeds below 1.0 px/cycle
- **Vector composition**: X/Y axes are processed as a combined vector, providing uniform speed in all directions including diagonals
- **Circular deadzone**: Deadzone evaluated against the magnitude of the combined vector — uniform in all directions
- **Quadratic acceleration**: Acceleration proportional to the square of the tilt amount — slow at start, faster as you push further
- **Proportional deceleration**: Returning the stick reduces speed proportionally; current speed converges exponentially toward the speed limit for the current tilt
- **Moving average filter**: ADC noise reduction
- **Startup calibration**: TMR sensor warmup delay and automatic center value acquisition
- **Asymmetric range correction**: Per-direction normalization even when the center output is not at the midpoint of the ADC range
- **Button support**: Mouse click via SW pin (direct GPIO connection, optional)
- **Fully configurable**: All parameters can be overridden via `#define` in `config.h`

## Requirements

- **MCU**: RP2040 (RP2040-Zero, etc.)
- **QMK Firmware** (including Vial-compatible forks)
- **ChibiOS** (uses the RP2040 ADC driver)

## Files

| File | Description |
|---|---|
| `qmk_analog_stick.h` | Header file (default parameter definitions + API declarations) |
| `qmk_analog_stick.c` | Implementation file |
| `halconf.h` | ChibiOS HAL configuration (enables ADC) |
| `mcuconf.h` | ChibiOS MCU configuration (enables RP2040 ADC driver) |

## Quick Start

### 1. Place the Files

Copy `qmk_analog_stick.h`, `qmk_analog_stick.c`, `halconf.h`, and `mcuconf.h` into your keyboard directory. If `halconf.h` or `mcuconf.h` already exist, merge the content from steps 2 and 3 into the existing files.

```
keyboards/your_keyboard/
  ├── qmk_analog_stick.h
  ├── qmk_analog_stick.c
  ├── halconf.h
  ├── mcuconf.h
  └── keymaps/default/
      ├── config.h
      ├── keymap.c
      └── rules.mk
```

### 2. halconf.h

```c
#pragma once

#define HAL_USE_ADC TRUE

#include_next <halconf.h>
```

### 3. mcuconf.h

```c
#pragma once

#include_next <mcuconf.h>

#undef RP_ADC_USE_ADC1
#define RP_ADC_USE_ADC1 TRUE
```

### 4. config.h

At minimum, define the joystick connection pins.

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29

// Optional: enable button support
// #define JOYSTICK_SW_PIN GP13
```

### 5. keymap.c

```c
#include QMK_KEYBOARD_H
#include "qmk_analog_stick.h"

// ... keymap definitions ...

void keyboard_post_init_user(void) {
    analog_stick_init();
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    return analog_stick_update(mouse_report);
}
```

### 6. rules.mk

```makefile
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom
SRC += analog.c qmk_analog_stick.c
```

## API Reference

### `void analog_stick_init(void)`

Initializes the joystick. Call this inside `keyboard_post_init_user()`.

**What it does:**

1. Waits `JOYSTICK_WARMUP_MS` milliseconds for the TMR sensor to stabilize
2. Samples the X/Y axes 64 times to calibrate center values
3. Initializes the moving average buffer with the center values
4. If `JOYSTICK_SW_PIN` is defined, configures the button pin as input with pull-up
5. If debug is enabled, prints calibration results to the console

**Note:** Do not touch the stick during initialization. Any deflection will be recorded as the center value.

---

### `report_mouse_t analog_stick_update(report_mouse_t mouse_report)`

Updates the mouse report on every scan cycle. Call this inside `pointing_device_task_user()`.

**Arguments:**

| Name | Type | Description |
|---|---|---|
| `mouse_report` | `report_mouse_t` | Current mouse report |

**Returns:**

Updated mouse report with `mouse_report.x`, `mouse_report.y`, and (if `JOYSTICK_SW_PIN` is defined) `mouse_report.buttons` set.

**What it does:**

1. ADC read + moving average filter
2. Normalize each axis to -1000~+1000 (with asymmetric range correction)
3. Calculate the combined vector magnitude (X/Y)
4. Circular deadzone check
5. Calculate speed limit (speed_limit) from current tilt amount (quadratic curve)
6. If `current_speed < speed_limit`: accelerate; if `current_speed > speed_limit`: proportionally decelerate
7. Distribute combined speed to X/Y axes by direction ratio
8. Sub-pixel accumulation + integer pixel conversion
9. Read button state (if `JOYSTICK_SW_PIN` is defined)

## Architecture

### Speed Determination Flow

```
Stick tilt
     ↓
Normalize each axis (-1000~+1000)
     ↓
Combined vector: magnitude = √(x² + y²)
     ↓
Circular deadzone check (magnitude > deadzone?)
     ↓  YES
Subtract deadzone → adjusted_magnitude (0~1000)
     ↓
Speed limit: speed_limit = adjusted_magnitude² × MAX_SPEED / 1000000
     ↓
current_speed < speed_limit?  → Accelerate: current_speed += adjusted_magnitude² × ACCEL_RATE / 1000000
current_speed > speed_limit?  → Decelerate: current_speed -= (current_speed - speed_limit) × DECEL_RATE / 100
     ↓
Direction distribution: speed_x = current_speed × norm_x / magnitude
                        speed_y = current_speed × norm_y / magnitude
     ↓
Sub-pixel accumulation → mouse_report.x, mouse_report.y
```

### How Acceleration Works

The **square** of the tilt amount becomes the per-cycle acceleration. Holding the stick continuously builds up speed.

```
Acceleration/cycle = (adjusted_magnitude / 1000)² × ACCEL_RATE
Speed limit        = (adjusted_magnitude / 1000)² × MAX_SPEED
```

- **Small tilt**: low acceleration → slow buildup → precise control
- **Large tilt**: high acceleration → fast buildup → quick movement
- **Any tilt**: speed can reach its corresponding speed limit (only the time differs)
- **Release to deadzone**: cursor coasts to a stop using the last known direction

| Tilt | Accel/cycle | Speed limit (MAX_SPEED=8000) |
|---|---|---|
| 30% | 0.00144 | 720 (0.72 px/cycle) |
| 50% | 0.004   | 2000 (2.0 px/cycle) |
| 70% | 0.00784 | 3920 (3.92 px/cycle) |
| 100% | 0.016  | 8000 (8.0 px/cycle) |

### How Deceleration Works

When the stick is returned toward center, `speed_limit` drops. If `current_speed` exceeds it, `DECEL_RATE%` of the difference is shed each cycle (exponential decay).

```
Deceleration/cycle = (current_speed - speed_limit) × DECEL_RATE / 100
```

- **Small return**: speed_limit drops slightly → gentle deceleration
- **Large return**: speed_limit drops sharply → rapid deceleration
- **Return to deadzone**: cursor coasts to a stop using the last known direction

**Deceleration example with DECEL_RATE=50 (default):**

Full tilt (speed=8000) → return to 50% (speed_limit=2000):

| Frame | Speed |
|---|---|
| 0 | 8000 |
| 1 | 5000 |
| 2 | 3500 |
| 3 | 2750 |
| 5 | 2100 |
| 8 | ≈2000 |

Reaches the target speed in approximately 80ms (8 frames @ 100Hz).

### Circular Deadzone

The deadzone is evaluated against the magnitude of the combined X/Y vector.

```
Square deadzone (before)    This library (circular)
+---+---+                       .---.
|   |   |                      /     \
+---+---+                     |   o   |
|   |   |                      \     /
+---+---+                       '---'
```

A square deadzone causes one axis to react before the other in diagonal directions, producing a four-directional pull effect. The circular deadzone provides a uniform deadzone in all directions.

### Vector Composition

Processing X and Y axes independently leads to non-uniform speed in diagonal directions. This library calculates acceleration against the combined vector magnitude, then distributes it to X/Y by direction ratio.

```
magnitude = √(norm_x² + norm_y²)
speed_x = current_speed × norm_x / magnitude
speed_y = current_speed × norm_y / magnitude
```

## Parameter Reference

All parameters can be overridden in `config.h` using `#define`. If not defined, the default value is used.

### Required Parameters

| Parameter | Description | Example |
|---|---|---|
| `JOYSTICK_X_PIN` | ADC pin for the X axis | `GP28` |
| `JOYSTICK_Y_PIN` | ADC pin for the Y axis | `GP29` |

### Button Parameters

Defining `JOYSTICK_SW_PIN` enables button support. If not defined, no button-related code is compiled in.

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_SW_PIN` | (undefined) | GPIO pin for SW. Defining this enables button support |
| `JOYSTICK_SW_BUTTON` | `MOUSE_BTN1` | Mouse button assigned to the joystick switch |

**Available mouse button constants:**

| Constant | Description |
|---|---|
| `MOUSE_BTN1` | Left click (default) |
| `MOUSE_BTN2` | Right click |
| `MOUSE_BTN3` | Middle click (wheel click) |
| `MOUSE_BTN4` | Back |
| `MOUSE_BTN5` | Forward |

**Examples:**

```c
// Left click (default)
#define JOYSTICK_SW_PIN GP13

// Change to right click
#define JOYSTICK_SW_PIN GP13
#define JOYSTICK_SW_BUTTON MOUSE_BTN2
```

**Wiring:**

The joystick SW pin connects to internal GND when pressed (active LOW), so connecting it directly to a GPIO is sufficient. No external pull-up resistor is needed (MCU internal pull-up is used).

```
Joystick SW -----> GPIO pin (internal pull-up enabled)
```

> **Note:** The joystick SW pin cannot be connected directly to a key matrix. SW is a single-pin output that shorts to internal GND when pressed and cannot function as a ROW-COL matrix connection. Connect it directly to a GPIO pin.

### Speed / Acceleration Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_MAX_SPEED` | `8000` | Maximum speed (x1000 scale; 8000 = 8.0 px/cycle) |
| `JOYSTICK_ACCEL_RATE` | `16` | Acceleration coefficient — higher values accelerate faster |
| `JOYSTICK_DECEL_RATE` | `50` | Deceleration rate (%). When returning the stick, this percentage of the speed excess is shed each cycle |

- `MAX_SPEED`: Speed limit at full deflection. The speed limit at any tilt is `(tilt%)² × MAX_SPEED`
- `ACCEL_RATE`: Per-cycle acceleration is the square of the tilt amount multiplied by this value. At full deflection, the speed limit is reached in approximately 5 seconds
- `DECEL_RATE`: When the stick is returned, this percentage of `(current_speed - speed_limit)` is removed each cycle. Higher values track stick position more quickly

**ACCEL_RATE reference:**

| ACCEL_RATE | Time to reach speed limit at full deflection |
|---|---|
| `8`  | ~10 seconds |
| `16` | ~5 seconds (default) |
| `32` | ~2.5 seconds |
| `64` | ~1.25 seconds |

**DECEL_RATE reference (full deflection → 50% return):**

| DECEL_RATE | Frames to reach target speed (100Hz) |
|---|---|
| `30` | ~15 frames (150ms) |
| `50` | ~8 frames (80ms) (default) |
| `80` | ~4 frames (40ms) |

### ADC Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_ADC_MIN` | `0` | Measured minimum ADC value (shared default for both axes) |
| `JOYSTICK_ADC_MAX` | `1023` | Measured maximum ADC value (shared default for both axes) |
| `JOYSTICK_ADC_X_MIN` | `JOYSTICK_ADC_MIN` | Measured minimum ADC value for X axis |
| `JOYSTICK_ADC_X_MAX` | `JOYSTICK_ADC_MAX` | Measured maximum ADC value for X axis |
| `JOYSTICK_ADC_Y_MIN` | `JOYSTICK_ADC_MIN` | Measured minimum ADC value for Y axis |
| `JOYSTICK_ADC_Y_MAX` | `JOYSTICK_ADC_MAX` | Measured maximum ADC value for Y axis |

For joysticks where the X and Y axes have different output ranges (e.g. K-SILVER JH16), each axis can be configured independently. If `JOYSTICK_ADC_X_MIN` etc. are not defined, `JOYSTICK_ADC_MIN` / `JOYSTICK_ADC_MAX` are used for both axes.

```c
// Example: Y axis has a smaller maximum value
#define JOYSTICK_ADC_Y_MAX  784
```

### Filter Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEADZONE` | `15` | Center deadzone radius (in ADC units) |
| `JOYSTICK_SMOOTHING` | `4` | Moving average sample count |

- `DEADZONE`: Circular deadzone radius in ADC units. If the combined vector magnitude is within this value, the cursor does not move. Smaller = more sensitive; larger = more stable
- `SMOOTHING`: Moving average window size. Larger = smoother, but with more input latency

### Startup Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_WARMUP_MS` | `3000` | TMR sensor stabilization wait time (ms) |

TMR sensors produce unstable output immediately after power-on, so this delay is applied before calibration.

### Debug Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEBUG` | `1` | Debug output (`1`: enabled, `0`: disabled) |

When enabled, the following information is printed to the console (requires `"console": true` in `keyboard.json`) approximately once per second:

```
Xr=450 cx=449 dx=0 | Yr=520 cy=519 dy=0 | spd=0
```

| Field | Description |
|---|---|
| `Xr` / `Yr` | Smoothed ADC raw values |
| `cx` / `cy` | Calibrated center values |
| `dx` / `dy` | Mouse movement output (pixels) |
| `spd` | Current speed (x1000 scale) |

To view debug output:
- **QMK Toolbox**: Connect the keyboard and console output appears automatically
- **qmk console**: Run `qmk console` in a terminal

## Configuration Examples

### Precision-focused (CAD / design work)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_DEADZONE      60
#define JOYSTICK_MAX_SPEED   3000   // 3.0
#define JOYSTICK_ACCEL_RATE     4   // ~20 seconds to MAX at full deflection
#define JOYSTICK_DECEL_RATE    30   // gradual deceleration
```

### Speed-focused (browsing / gaming)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_DEADZONE      20
#define JOYSTICK_MAX_SPEED  15000   // 15.0
#define JOYSTICK_ACCEL_RATE    64   // ~1.25 seconds to MAX at full deflection
#define JOYSTICK_DECEL_RATE    80   // quick deceleration
```

### With button (left click)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_SW_PIN      GP13
```

### With button (right click)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_SW_PIN      GP13
#define JOYSTICK_SW_BUTTON   MOUSE_BTN2
```

### Minimal (no button, default settings)

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29
```

## Hardware Notes

### Supported Joysticks

This library is broadly compatible with any joystick that outputs analog voltage read via ADC. Confirmed working models:

- **K-SILVER JS16** (TMR sensing)
- **K-SILVER JH16** (Hall Effect sensing)

### K-SILVER JS16 / JH16

- **Sensing method**: TMR (Tunnel Magnetoresistance) / Hall Effect
- **Operating voltage**: 1.8V – 3.3V
- **Current consumption**: ~210–215 µA
- **Durability**: ~5 million rotations
- **Pins**: VCC, GND, X output, Y output, SW (button)

### RP2040 ADC Pins

The RP2040 has four pins available for ADC use:

| Pin | ADC Channel |
|---|---|
| GP26 | ADC0 |
| GP27 | ADC1 |
| GP28 | ADC2 |
| GP29 | ADC3 |

**Note**: On some boards, GP29 may be reserved for VSYS voltage monitoring. In that case, use GP26, GP27, or GP28.

### Wiring Example

```
Joystick      RP2040-Zero
----------    -----------
VCC    ----->  3.3V
GND    ----->  GND
X out  ----->  GP28  (ADC pin)
Y out  ----->  GP29  (ADC pin)
SW     ----->  GP13  (any GPIO, optional)
```

### Wiring When Reusing an EC12 Encoder Footprint

The K-SILVER JS16/JH16 can be mounted in an EC12/EC11 rotary encoder footprint, but the pin roles differ — take care with wiring.

```
EC12 pin       Joystick pin   Connect to
---------      ------------   ----------
Encoder A  --> X output   --> ADC pin (GP28, etc.)
Encoder GND -> GND        --> GND
Encoder B  --> Y output   --> ADC pin (GP29, etc.)
Switch 1   --> VCC        --> 3.3V power rail (※)
Switch 2   --> SW         --> GPIO pin (※)
```

> **(※) Note:** EC12 switch pins are normally connected to the key matrix ROW/COL lines. The joystick VCC requires a continuous 3.3V supply, so verify that the Switch 1 pad is connected to the 3.3V power rail. Switch 2 (SW) must be wired directly to a GPIO, not to the matrix.

## Troubleshooting

### Cursor moves on its own

- Increase `JOYSTICK_DEADZONE` (e.g., `80`)
- Increase `JOYSTICK_WARMUP_MS` to improve calibration accuracy (e.g., `5000`)
- Do not touch the stick during startup calibration

### Cursor does not move

- Check that `HAL_USE_ADC TRUE` is defined in `halconf.h`
- Check that `RP_ADC_USE_ADC1 TRUE` is defined in `mcuconf.h`
- Check that `SRC += analog.c` is included in `rules.mk`
- Check that joystick VCC is connected to 3.3V
- Enable `JOYSTICK_DEBUG 1` and check the console to confirm ADC values are changing

### Speed differs by direction

- This occurs when the TMR sensor center output is not at the midpoint of the ADC range
- This library handles it automatically via vector composition and per-direction scaling
- For extreme asymmetry, set `JOYSTICK_ADC_MIN` / `JOYSTICK_ADC_MAX` to measured values

### Cursor only moves in 4 directions / pulled toward 4 directions

- This library addresses this with vector composition and a circular deadzone
- If it still occurs, reduce `JOYSTICK_ACCEL_RATE` to limit maximum speed
- Increasing `JOYSTICK_SMOOTHING` smooths out the input

### Button does not respond

- Check that `JOYSTICK_SW_PIN` is defined in `config.h`
- Check that the SW pin is connected directly to a GPIO (not through the key matrix)
- Use a multimeter to confirm continuity between SW and GND when pressed

### Button is stuck pressed

- Check that the SW pin is not shorted to GND
- Check the wiring (SW is active LOW: pressing connects it to GND)

## License

MIT

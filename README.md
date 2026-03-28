# JS16 TMR Joystick Library for QMK

K-SILVER JS16 TMR (Tunnel Magnetoresistance) ジョイスティックを QMK/Vial ファームウェアでマウスカーソル操作に使用するためのライブラリです。

## Features

- **サブピクセル処理**: x1000 スケールの内部精度で、速度 1.0 未満でも滑らかなカーソル移動
- **ベクトル合成**: X/Y 軸を合成ベクトルとして処理し、斜め方向も均一な速度で移動
- **円形デッドゾーン**: 合成ベクトルの大きさで判定。全方向均一なデッドゾーン
- **二乗加速**: スティックの傾き量の二乗に比例した加速度。傾け始めはゆっくり、大きく倒すほど速く加速
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
| `js16_joystick.h` | ヘッダファイル (デフォルトパラメータ定義 + API 宣言) |
| `js16_joystick.c` | 実装ファイル |
| `halconf.h` | ChibiOS HAL 設定 (ADC 有効化) |
| `mcuconf.h` | ChibiOS MCU 設定 (RP2040 ADC ドライバ有効化) |

## Quick Start

### 1. ファイルの配置

`js16_joystick.h`、`js16_joystick.c`、`halconf.h`、`mcuconf.h` をキーボードディレクトリにコピーします。既に `halconf.h` や `mcuconf.h` が存在する場合は、手順 2・3 の内容を既存ファイルに追記してください。

```
keyboards/your_keyboard/
  ├── js16_joystick.h
  ├── js16_joystick.c
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
#include "js16_joystick.h"

// ... キーマップ定義 ...

void keyboard_post_init_user(void) {
    js16_init();
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    return js16_update(mouse_report);
}
```

### 6. rules.mk

```makefile
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom
SRC += analog.c js16_joystick.c
```

## API Reference

### `void js16_init(void)`

ジョイスティックを初期化します。`keyboard_post_init_user()` 内で呼び出してください。

**処理内容:**

1. `JOYSTICK_WARMUP_MS` ミリ秒待機 (TMR センサー安定化)
2. 64 回のサンプリングで X/Y 軸の中心値をキャリブレーション
3. 移動平均バッファを中心値で初期化
4. `JOYSTICK_SW_PIN` 定義時、ボタンピンを入力プルアップに設定
5. デバッグ有効時、キャリブレーション結果をコンソール出力

**注意:** 初期化中はスティックに触れないでください。触れた状態の値が中心値として記録されます。

---

### `report_mouse_t js16_update(report_mouse_t mouse_report)`

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
5. デッドゾーン差し引き後の傾き量で二乗加速
6. 合成速度を X/Y 方向比率で分配
7. サブピクセル蓄積 + 整数ピクセル変換
8. ボタン状態読み取り (`JOYSTICK_SW_PIN` 定義時)

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
デッドゾーン差し引き → adjusted_magnitude
     ↓
加速度 = adjusted_magnitude² × ACCEL_RATE
     ↓
current_speed += 加速度  (毎サイクル蓄積、MAX_SPEED で上限)
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
```

- **少し倒す**: 加速度が小さい → ゆっくり加速 → 精密操作
- **大きく倒す**: 加速度が大きい → 速く加速 → 素早い移動
- **どの傾きでも** MAX_SPEED まで到達可能（時間の差だけ）
- **スティックを離す**: 速度が即座にリセット

| 傾き | 加速度/cycle | MAX_SPEED(8.0) 到達時間 |
|---|---|---|
| 20% | 0.00064 | 約200秒 |
| 50% | 0.004 | 約20秒 |
| 70% | 0.008 | 約10秒 |
| 100% | 0.016 | 約5秒 |

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

JS16 の SW ピンは押下時に内部 GND に接続される（アクティブ LOW）ため、GPIO に直接接続するだけで動作します。外部プルアップ抵抗は不要です（MCU 内部プルアップを使用）。

```
JS16 SW -----> GPIOピン（内部プルアップ有効）
```

> **注意:** JS16 の SW ピンはキーマトリクスには直接接続できません。SW は押下時に内部 GND に短絡する 1 ピン出力のため、マトリクスの ROW-COL 間接続として機能しません。GPIO 直結で使用してください。

### Speed / Acceleration Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_MAX_SPEED` | `8000` | 最大速度 (x1000 スケール、8000 = 8.0 px/cycle) |
| `JOYSTICK_ACCEL_RATE` | `16` | 加速度係数。大きいほど速く加速する |

- `MAX_SPEED`: 速度の上限。どの傾き量でもこの速度を超えません
- `ACCEL_RATE`: 傾き量の二乗にこの値を掛けたものが毎サイクルの加速度になります。全倒しで約5秒で MAX_SPEED に到達します

**ACCEL_RATE の目安:**

| ACCEL_RATE | 全倒し時の MAX_SPEED(8000) 到達時間 |
|---|---|
| `8` | 約10秒 |
| `16` | 約5秒 (デフォルト) |
| `32` | 約2.5秒 |
| `64` | 約1.25秒 |

### ADC Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_ADC_MIN` | `5` | ADC の実測最小値 |
| `JOYSTICK_ADC_MAX` | `1023` | ADC の実測最大値 |

実際のジョイスティックで計測した値を設定してください。方向ごとのスケーリング計算に使用されます。

### Filter Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEADZONE` | `40` | 中心付近のデッドゾーン (ADC 値) |
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
```

### 高速操作重視 (ブラウジング / ゲーム向け)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JOYSTICK_DEADZONE      20
#define JOYSTICK_MAX_SPEED  15000   // 15.0
#define JOYSTICK_ACCEL_RATE    64   // 全倒しで約1.25秒でMAX
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

### K-SILVER JS16 TMR Joystick

- **センシング方式**: TMR (トンネル磁気抵抗効果)
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
JS16          RP2040-Zero
----          -----------
VCC    ----->  3.3V
GND    ----->  GND
X out  ----->  GP28  (ADC ピン)
Y out  ----->  GP29  (ADC ピン)
SW     ----->  GP13  (任意の GPIO、オプション)
```

### EC12 エンコーダーフットプリント流用時の配線

JS16 は EC12/EC11 ロータリーエンコーダーのフットプリントに実装できますが、ピンの役割が異なるため配線に注意が必要です。

```
EC12 ピン      JS16 ピン      接続先
---------      ---------      ------
Encoder A  --> X output   --> ADC ピン (GP28 等)
Encoder GND -> GND        --> GND
Encoder B  --> Y output   --> ADC ピン (GP29 等)
Switch 1   --> VCC        --> 3.3V 電源ライン (※)
Switch 2   --> SW         --> GPIO ピン (※)
```

> **(※) 注意:** EC12 のスイッチピンは通常キーマトリクスの ROW/COL に接続されています。JS16 の VCC は常時 3.3V 給電が必要なため、Switch 1 のパッドが 3.3V 電源ラインに接続されていることを確認してください。Switch 2 (SW) はマトリクスではなく GPIO に直結する必要があります。

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

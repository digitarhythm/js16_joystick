// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: MIT
//
// QMK Analog Stick Library
// アナログジョイスティックをマウスカーソル操作に使用するライブラリ
//
// 使い方:
//   1. config.h で必須パラメータ（JOYSTICK_X_PIN, JOYSTICK_Y_PIN）を定義
//   2. config.h で任意のパラメータを上書き定義（デフォルト値あり）
//   3. keymap.c で #include "qmk_analog_stick.h" する
//   4. keyboard_post_init_user() 内で analog_stick_init() を呼ぶ
//   5. pointing_device_task_user() 内で analog_stick_update() を呼ぶ
//   6. rules.mk に SRC += qmk_analog_stick.c を追加
//   7. rules.mk に POINTING_DEVICE_ENABLE = yes / POINTING_DEVICE_DRIVER = custom を追加
//   8. halconf.h に HAL_USE_ADC TRUE を定義
//   9. mcuconf.h に RP_ADC_USE_ADC1 TRUE を定義

#pragma once

#include "quantum.h"
#include "pointing_device.h"
#include "analog.h"

// ===== 必須パラメータ（config.h で定義必須） =====
// #define JOYSTICK_X_PIN GP28
// #define JOYSTICK_Y_PIN GP29

// ===== オプションパラメータ（config.h で上書き可能） =====

// ボタンピン（定義するとマウスクリック機能が有効になる）
// #define JOYSTICK_SW_PIN GP13

// ボタンに割り当てるマウスボタン
#ifndef JOYSTICK_SW_BUTTON
#define JOYSTICK_SW_BUTTON MOUSE_BTN1   // 左クリック
#endif

// 中心付近の不感帯（ADC値）
#ifndef JOYSTICK_DEADZONE
#define JOYSTICK_DEADZONE    30
#endif

// ADC値の実測範囲（共通デフォルト値）
#ifndef JOYSTICK_ADC_MIN
#define JOYSTICK_ADC_MIN      0
#endif
#ifndef JOYSTICK_ADC_MAX
#define JOYSTICK_ADC_MAX   1023
#endif

// X軸ADC範囲（未定義の場合は JOYSTICK_ADC_MIN/MAX を使用）
#ifndef JOYSTICK_ADC_X_MIN
#define JOYSTICK_ADC_X_MIN JOYSTICK_ADC_MIN
#endif
#ifndef JOYSTICK_ADC_X_MAX
#define JOYSTICK_ADC_X_MAX JOYSTICK_ADC_MAX
#endif

// Y軸ADC範囲（未定義の場合は JOYSTICK_ADC_MIN/MAX を使用）
#ifndef JOYSTICK_ADC_Y_MIN
#define JOYSTICK_ADC_Y_MIN JOYSTICK_ADC_MIN
#endif
#ifndef JOYSTICK_ADC_Y_MAX
#define JOYSTICK_ADC_Y_MAX JOYSTICK_ADC_MAX
#endif

// 速度設定（x1000スケール: 1000 = 1.0ピクセル/サイクル）
#ifndef JOYSTICK_MAX_SPEED
#define JOYSTICK_MAX_SPEED  6000   // 最大速度（8.0）
#endif

// 加速度設定
// 傾き量の二乗 × この値が毎サイクルの加速度になる
// 大きいほど速く加速する
#ifndef JOYSTICK_ACCEL_RATE
#define JOYSTICK_ACCEL_RATE   16   // 全倒しで約5秒でMAX_SPEEDに到達
#endif

// 減速率（%/サイクル）
// スティックを戻したとき、目標速度との差のこの割合ずつ減速する
// 大きいほどスティックの戻し量に素早く追従する（0〜100）
#ifndef JOYSTICK_DECEL_RATE
#define JOYSTICK_DECEL_RATE   8
#endif

// 移動平均サンプル数
#ifndef JOYSTICK_SMOOTHING
#define JOYSTICK_SMOOTHING    4
#endif

// TMRセンサー安定待ち時間
#ifndef JOYSTICK_WARMUP_MS
#define JOYSTICK_WARMUP_MS 1000
#endif

// デバッグ出力の有効/無効
#ifndef JOYSTICK_DEBUG
#define JOYSTICK_DEBUG        1    // 1:有効 0:無効
#endif

// ===== API =====

// 初期化（keyboard_post_init_user 内で呼ぶ）
// ウォームアップ待機 → キャリブレーション を実行
void analog_stick_init(void);

// マウスレポート更新（pointing_device_task_user 内で呼ぶ）
// スムージング、デッドゾーン、加速カーブ、サブピクセル処理を適用
report_mouse_t analog_stick_update(report_mouse_t mouse_report);

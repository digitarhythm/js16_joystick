// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// JS16 TMR Joystick Library for QMK
// K-SILVER JS16 TMR ジョイスティックをマウスカーソル操作に使用するライブラリ
//
// 使い方:
//   1. config.h で必須パラメータ（JOYSTICK_X_PIN, JOYSTICK_Y_PIN）を定義
//   2. config.h で任意のパラメータを上書き定義（デフォルト値あり）
//   3. keymap.c で #include "js16_joystick.h" する
//   4. keyboard_post_init_user() 内で js16_init() を呼ぶ
//   5. pointing_device_task_user() 内で js16_update() を呼ぶ
//   6. rules.mk に SRC += js16_joystick.c を追加
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

// 中心付近の不感帯（ADC値）
#ifndef JOYSTICK_DEADZONE
#define JOYSTICK_DEADZONE    40
#endif

// ADC値の実測範囲
#ifndef JOYSTICK_ADC_MIN
#define JOYSTICK_ADC_MIN      5
#endif
#ifndef JOYSTICK_ADC_MAX
#define JOYSTICK_ADC_MAX   1023
#endif

// 速度設定（x100スケール: 100 = 1.0ピクセル/サイクル）
#ifndef JOYSTICK_MAX_SPEED
#define JOYSTICK_MAX_SPEED  800    // 全倒し時の最大速度（8.0）
#endif
#ifndef JOYSTICK_NORM_SPEED
#define JOYSTICK_NORM_SPEED 200    // 通常範囲の最大速度（2.0）
#endif
#ifndef JOYSTICK_MID_SPEED
#define JOYSTICK_MID_SPEED   10    // THRESHOLD%傾斜時の速度（0.1）
#endif

// 速度カーブ設定
#ifndef JOYSTICK_THRESHOLD
#define JOYSTICK_THRESHOLD   75    // 加速が切り替わる傾斜%（0-100）
#endif

// 全倒し判定（raw値がこの範囲外で全倒し）
#ifndef JOYSTICK_FULL_LOW
#define JOYSTICK_FULL_LOW    30
#endif
#ifndef JOYSTICK_FULL_HIGH
#define JOYSTICK_FULL_HIGH  990
#endif

// 移動平均サンプル数
#ifndef JOYSTICK_SMOOTHING
#define JOYSTICK_SMOOTHING    4
#endif

// 時間加速設定
#ifndef JOYSTICK_ACCEL_TIME
#define JOYSTICK_ACCEL_TIME 30000   // MAX_SPEEDに達するまでの時間(ms)
#endif

// TMRセンサー安定待ち時間
#ifndef JOYSTICK_WARMUP_MS
#define JOYSTICK_WARMUP_MS 3000
#endif

// デバッグ出力の有効/無効
#ifndef JOYSTICK_DEBUG
#define JOYSTICK_DEBUG        1    // 1:有効 0:無効
#endif

// ===== API =====

// 初期化（keyboard_post_init_user 内で呼ぶ）
// ウォームアップ待機 → キャリブレーション を実行
void js16_init(void);

// マウスレポート更新（pointing_device_task_user 内で呼ぶ）
// スムージング、デッドゾーン、加速カーブ、サブピクセル処理を適用
report_mouse_t js16_update(report_mouse_t mouse_report);

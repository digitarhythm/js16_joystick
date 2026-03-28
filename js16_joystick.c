// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: MIT
//
// JS16 TMR Joystick Library for QMK

#include "js16_joystick.h"
#include "print.h"

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

#define JOYSTICK_ACCEL_MAX_COUNT (JOYSTICK_ACCEL_TIME / 10)

static uint16_t center_x = 512;
static uint16_t center_y = 512;

// 移動平均フィルタ用バッファ
static uint16_t buf_x[JOYSTICK_SMOOTHING];
static uint16_t buf_y[JOYSTICK_SMOOTHING];
static uint8_t  buf_idx = 0;

// サブピクセル蓄積用
static int32_t subpx_x = 0;
static int32_t subpx_y = 0;

// 時間加速カウンタ
static uint16_t hold_counter = 0;       // 通常加速用
static uint16_t full_tilt_counter = 0;  // 全倒し加速用

// デバッグタイマー
static uint16_t debug_timer = 0;

static uint16_t read_smoothed(pin_t pin, uint16_t *buf) {
    buf[buf_idx] = analogReadPin(pin);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < JOYSTICK_SMOOTHING; i++) {
        sum += buf[i];
    }
    return sum / JOYSTICK_SMOOTHING;
}

// 軸ごとの正規化（-1000〜+1000 にスケーリング）
static int16_t normalize_axis(uint16_t val, uint16_t center) {
    int16_t delta = (int16_t)val - (int16_t)center;

    if (abs(delta) < JOYSTICK_DEADZONE) return 0;

    // デッドゾーン分を差し引く
    int16_t normalized = (delta > 0) ? (delta - JOYSTICK_DEADZONE) : (delta + JOYSTICK_DEADZONE);

    // 方向ごとに実際の範囲で -1000〜+1000 にスケーリング
    int16_t range = (delta > 0)
        ? (JOYSTICK_ADC_MAX - center - JOYSTICK_DEADZONE)
        : (center - JOYSTICK_ADC_MIN - JOYSTICK_DEADZONE);
    if (range < 1) range = 1;

    int32_t scaled = (int32_t)normalized * 1000 / range;
    return (int16_t)constrain(scaled, -1000, 1000);
}

// 速度カーブ（0〜1000 の傾斜率 → x1000スケールの速度）
// 二乗カーブ: 傾け始めは超低速、大きく倒すほど加速
static int32_t apply_speed_curve(int32_t ratio) {
    return ratio * ratio * JOYSTICK_MID_SPEED / (1000 * 1000);
}

// 簡易整数平方根（ニュートン法）
static uint32_t isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

void js16_init(void) {
    // TMRセンサーが安定するまで待機
    wait_ms(JOYSTICK_WARMUP_MS);

    // キャリブレーション: 中心値を複数回サンプリングして平均化
    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    for (uint8_t i = 0; i < 64; i++) {
        sum_x += analogReadPin(JOYSTICK_X_PIN);
        sum_y += analogReadPin(JOYSTICK_Y_PIN);
        wait_ms(2);
    }
    center_x = sum_x / 64;
    center_y = sum_y / 64;

    // スムージングバッファを中心値で初期化
    for (uint8_t i = 0; i < JOYSTICK_SMOOTHING; i++) {
        buf_x[i] = center_x;
        buf_y[i] = center_y;
    }

#ifdef JOYSTICK_SW_PIN
    // ボタンピンを入力プルアップに設定
    setPinInputHigh(JOYSTICK_SW_PIN);
#endif

#if JOYSTICK_DEBUG
    uprintf("JS16 center: X=%u Y=%u\n", center_x, center_y);
#endif
}

report_mouse_t js16_update(report_mouse_t mouse_report) {
    uint16_t smooth_x = read_smoothed(JOYSTICK_X_PIN, buf_x);
    uint16_t smooth_y = read_smoothed(JOYSTICK_Y_PIN, buf_y);
    buf_idx = (buf_idx + 1) % JOYSTICK_SMOOTHING;

    // 各軸を -1000〜+1000 に正規化
    int16_t norm_x = normalize_axis(smooth_x, center_x);
    int16_t norm_y = normalize_axis(smooth_y, center_y);

    if (norm_x != 0 || norm_y != 0) {
        // 通常加速カウンタ
        if (hold_counter < JOYSTICK_ACCEL_MAX_COUNT) {
            hold_counter++;
        }

        // 合成ベクトルの大きさ（0〜1000）を計算
        uint32_t magnitude = isqrt((uint32_t)((int32_t)norm_x * norm_x + (int32_t)norm_y * norm_y));
        if (magnitude > 1000) magnitude = 1000;

        // 合成ベクトルに対して速度カーブを適用
        int32_t base_speed = apply_speed_curve(magnitude);

        // Phase 1: base_speed → NORM_SPEED（二乗カーブで加速）
        int32_t progress1 = (int32_t)hold_counter * hold_counter
                          / ((int32_t)JOYSTICK_ACCEL_MAX_COUNT * JOYSTICK_ACCEL_MAX_COUNT / 1000);
        int32_t phase1_speed = base_speed + (JOYSTICK_NORM_SPEED - base_speed) * progress1 / 1000;
        phase1_speed = constrain(phase1_speed, base_speed, JOYSTICK_NORM_SPEED);

        // 全倒し判定
        bool full_tilt = (smooth_x <= JOYSTICK_FULL_LOW) || (smooth_x >= JOYSTICK_FULL_HIGH)
                      || (smooth_y <= JOYSTICK_FULL_LOW) || (smooth_y >= JOYSTICK_FULL_HIGH);

        int32_t final_speed = phase1_speed;

        if (full_tilt) {
            // Phase 2: NORM_SPEED → MAX_SPEED（二乗カーブで加速、別カウンタ）
            if (full_tilt_counter < JOYSTICK_ACCEL_MAX_COUNT) {
                full_tilt_counter++;
            }
            int32_t progress2 = (int32_t)full_tilt_counter * full_tilt_counter
                              / ((int32_t)JOYSTICK_ACCEL_MAX_COUNT * JOYSTICK_ACCEL_MAX_COUNT / 1000);
            final_speed = phase1_speed + (JOYSTICK_MAX_SPEED - phase1_speed) * progress2 / 1000;
            final_speed = constrain(final_speed, phase1_speed, JOYSTICK_MAX_SPEED);
        } else {
            full_tilt_counter = 0;
        }

        // 合成速度を各軸に方向比率で分配
        int32_t speed_x = final_speed * (int32_t)norm_x / (int32_t)magnitude;
        int32_t speed_y = final_speed * (int32_t)norm_y / (int32_t)magnitude;

        // サブピクセル蓄積
        subpx_x += speed_x;
        subpx_y += speed_y;
    } else {
        hold_counter = 0;
        full_tilt_counter = 0;
        subpx_x = 0;
        subpx_y = 0;
    }

    // 蓄積値から整数ピクセルを取り出す
    mouse_report.x = (int8_t)constrain(subpx_x / 1000, -127, 127);
    mouse_report.y = (int8_t)constrain(subpx_y / 1000, -127, 127);
    subpx_x -= mouse_report.x * 1000;
    subpx_y -= mouse_report.y * 1000;

#ifdef JOYSTICK_SW_PIN
    // ボタン読み取り（アクティブLOW: 押すとGNDに接続）
    if (readPin(JOYSTICK_SW_PIN) == 0) {
        mouse_report.buttons |= JOYSTICK_SW_BUTTON;
    } else {
        mouse_report.buttons &= ~JOYSTICK_SW_BUTTON;
    }
#endif

#if JOYSTICK_DEBUG
    if (++debug_timer >= 100) {
        debug_timer = 0;
        uprintf("Xr=%u cx=%u dx=%d nx=%d | Yr=%u cy=%u dy=%d ny=%d | h=%u\n",
                smooth_x, center_x, mouse_report.x, norm_x,
                smooth_y, center_y, mouse_report.y, norm_y,
                hold_counter);
    }
#endif

    return mouse_report;
}

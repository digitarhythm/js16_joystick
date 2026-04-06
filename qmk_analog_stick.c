// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: MIT
//
// QMK Analog Stick Library

#include "qmk_analog_stick.h"
#include "print.h"

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

// (JOYSTICK_ACCEL_RATE: 傾き量に応じた加速度の係数)

static uint16_t center_x = 512;
static uint16_t center_y = 512;

// 移動平均フィルタ用バッファ
static uint16_t buf_x[JOYSTICK_SMOOTHING];
static uint16_t buf_y[JOYSTICK_SMOOTHING];
static uint8_t  buf_idx = 0;

// サブピクセル蓄積用
static int32_t subpx_x = 0;
static int32_t subpx_y = 0;

// 加速用
static int32_t current_speed = 0;    // 現在の速度（x1000スケール）
static int32_t accel_accum = 0;      // 加速度の端数蓄積

// 離し時の減速継続用（最後の有効方向を保持）
static int16_t  last_norm_x   = 0;
static int16_t  last_norm_y   = 0;
static uint32_t last_magnitude = 1;

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

// 軸ごとの正規化（-1000〜+1000 にスケーリング、デッドゾーンなし）
static int16_t normalize_axis(uint16_t val, uint16_t center, uint16_t adc_min, uint16_t adc_max) {
    int16_t delta = (int16_t)val - (int16_t)center;
    if (delta == 0) return 0;

    // 方向ごとに実際の範囲で -1000〜+1000 にスケーリング
    int16_t range = (delta > 0)
        ? (adc_max - center)
        : (center - adc_min);
    if (range < 1) range = 1;

    int32_t scaled = (int32_t)delta * 1000 / range;
    return (int16_t)constrain(scaled, -1000, 1000);
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

void analog_stick_init(void) {
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
    uprintf("AnalogStick center: X=%u Y=%u\n", center_x, center_y);
#endif
}

report_mouse_t analog_stick_update(report_mouse_t mouse_report) {
    uint16_t smooth_x = read_smoothed(JOYSTICK_X_PIN, buf_x);
    uint16_t smooth_y = read_smoothed(JOYSTICK_Y_PIN, buf_y);
    buf_idx = (buf_idx + 1) % JOYSTICK_SMOOTHING;

    // 各軸を -1000〜+1000 に正規化
    int16_t norm_x = normalize_axis(smooth_x, center_x, JOYSTICK_ADC_X_MIN, JOYSTICK_ADC_X_MAX);
    int16_t norm_y = normalize_axis(smooth_y, center_y, JOYSTICK_ADC_Y_MIN, JOYSTICK_ADC_Y_MAX);

    // 合成ベクトルの大きさ（0〜1000）を計算
    uint32_t magnitude = isqrt((uint32_t)((int32_t)norm_x * norm_x + (int32_t)norm_y * norm_y));
    if (magnitude > 1000) magnitude = 1000;

    // 円形デッドゾーン: 合成ベクトルで判定（DEADZONE を 0〜1000 スケールに変換）
    uint32_t deadzone_normalized = (uint32_t)JOYSTICK_DEADZONE * 1000 /
        ((JOYSTICK_ADC_MAX - JOYSTICK_ADC_MIN) / 2);

    if (magnitude > deadzone_normalized) {
        // デッドゾーン分を差し引いてスケーリング
        int32_t effective = (int32_t)magnitude - (int32_t)deadzone_normalized;
        int32_t effective_max = 1000 - (int32_t)deadzone_normalized;
        if (effective_max < 1) effective_max = 1;
        uint32_t adjusted_magnitude = (uint32_t)effective * 1000 / effective_max;
        if (adjusted_magnitude > 1000) adjusted_magnitude = 1000;

        // 現在の傾き量に基づく速度上限（二乗カーブ）
        int32_t speed_limit = (int32_t)(adjusted_magnitude * adjusted_magnitude / 1000) * JOYSTICK_MAX_SPEED / 1000;

        if (current_speed < speed_limit) {
            // 加速: 傾き量の二乗を加速度として蓄積
            accel_accum += (int32_t)adjusted_magnitude * adjusted_magnitude * JOYSTICK_ACCEL_RATE;
            current_speed += accel_accum / 1000000;
            accel_accum %= 1000000;
            if (current_speed > speed_limit) current_speed = speed_limit;
        } else if (current_speed > speed_limit) {
            // 減速: スティックを戻した量に応じて速度を落とす
            int32_t decel = (current_speed - speed_limit) * JOYSTICK_DECEL_RATE / 100;
            if (decel < 1) decel = 1;
            current_speed -= decel;
            if (current_speed < speed_limit) current_speed = speed_limit;
            accel_accum = 0;
        }

        // 最後の有効方向を保存（離し時の減速継続用）
        last_norm_x   = norm_x;
        last_norm_y   = norm_y;
        last_magnitude = magnitude;

        // 合成速度を各軸に方向比率で分配
        int32_t speed_x = current_speed * (int32_t)norm_x / (int32_t)magnitude;
        int32_t speed_y = current_speed * (int32_t)norm_y / (int32_t)magnitude;

        // サブピクセル蓄積
        subpx_x += speed_x;
        subpx_y += speed_y;
    } else {
        // デッドゾーン内 - 最後の方向で緩やかに減速
        accel_accum = 0;
        if (current_speed > 0) {
            int32_t decel = current_speed * JOYSTICK_DECEL_RATE / 100;
            if (decel < 1) decel = 1;
            current_speed -= decel;
            if (current_speed < 0) current_speed = 0;

            int32_t speed_x = current_speed * (int32_t)last_norm_x / (int32_t)last_magnitude;
            int32_t speed_y = current_speed * (int32_t)last_norm_y / (int32_t)last_magnitude;
            subpx_x += speed_x;
            subpx_y += speed_y;
        } else {
            subpx_x = 0;
            subpx_y = 0;
        }
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
        uprintf("Xr=%u cx=%u dx=%d | Yr=%u cy=%u dy=%d | spd=%ld\n",
                smooth_x, center_x, mouse_report.x,
                smooth_y, center_y, mouse_report.y,
                current_speed);
    }
#endif

    return mouse_report;
}

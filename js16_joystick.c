// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: GPL-2.0-or-later
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
static int16_t subpx_x = 0;
static int16_t subpx_y = 0;

// 時間加速カウンタ
static uint16_t hold_counter = 0;

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

static int16_t get_joystick_delta(uint16_t val, uint16_t center) {
    int16_t delta = (int16_t)val - (int16_t)center;

    if (abs(delta) < JOYSTICK_DEADZONE) return 0;

    // デッドゾーン分を差し引く
    int16_t normalized = (delta > 0) ? (delta - JOYSTICK_DEADZONE) : (delta + JOYSTICK_DEADZONE);

    // 方向ごとに実際の範囲でスケーリング
    int16_t range = (delta > 0)
        ? (JOYSTICK_ADC_MAX - center - JOYSTICK_DEADZONE)
        : (center - JOYSTICK_ADC_MIN - JOYSTICK_DEADZONE);
    if (range < 1) range = 1;

    // 2段階カーブ（x100スケール）
    int8_t sign = (normalized > 0) ? 1 : -1;
    int32_t abs_norm = abs(normalized);
    int32_t threshold = (int32_t)range * JOYSTICK_THRESHOLD / 100;
    int32_t speed;
    if (threshold < 1) threshold = 1;
    if (abs_norm <= threshold) {
        speed = abs_norm * JOYSTICK_MID_SPEED / threshold;
    } else {
        int32_t remain = range - threshold;
        if (remain < 1) remain = 1;
        speed = JOYSTICK_MID_SPEED
              + (abs_norm - threshold) * (JOYSTICK_MAX_SPEED - JOYSTICK_MID_SPEED)
              / remain;
    }

    return (int16_t)constrain(sign * speed, -JOYSTICK_MAX_SPEED, JOYSTICK_MAX_SPEED);
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

#if JOYSTICK_DEBUG
    uprintf("JS16 center: X=%u Y=%u\n", center_x, center_y);
#endif
}

report_mouse_t js16_update(report_mouse_t mouse_report) {
    uint16_t smooth_x = read_smoothed(JOYSTICK_X_PIN, buf_x);
    uint16_t smooth_y = read_smoothed(JOYSTICK_Y_PIN, buf_y);
    buf_idx = (buf_idx + 1) % JOYSTICK_SMOOTHING;

    int16_t base_dx = get_joystick_delta(smooth_x, center_x);
    int16_t base_dy = get_joystick_delta(smooth_y, center_y);

    if (base_dx != 0 || base_dy != 0) {
        // スティック操作中: hold_counterを増加
        if (hold_counter < JOYSTICK_ACCEL_MAX_COUNT) {
            hold_counter++;
        }

        // 軸ごとに全倒し判定
        bool full_x = (smooth_x <= JOYSTICK_FULL_LOW) || (smooth_x >= JOYSTICK_FULL_HIGH);
        bool full_y = (smooth_y <= JOYSTICK_FULL_LOW) || (smooth_y >= JOYSTICK_FULL_HIGH);
        int32_t cap_x = full_x ? JOYSTICK_MAX_SPEED : JOYSTICK_NORM_SPEED;
        int32_t cap_y = full_y ? JOYSTICK_MAX_SPEED : JOYSTICK_NORM_SPEED;

        // 時間経過で加速（x100スケール）
        int8_t sign_x = (base_dx > 0) ? 1 : (base_dx < 0) ? -1 : 0;
        int8_t sign_y = (base_dy > 0) ? 1 : (base_dy < 0) ? -1 : 0;
        int32_t abs_dx = abs(base_dx);
        int32_t abs_dy = abs(base_dy);
        if (abs_dx > cap_x) abs_dx = cap_x;
        if (abs_dy > cap_y) abs_dy = cap_y;
        int32_t accel_dx = abs_dx + (cap_x - abs_dx) * hold_counter / JOYSTICK_ACCEL_MAX_COUNT;
        int32_t accel_dy = abs_dy + (cap_y - abs_dy) * hold_counter / JOYSTICK_ACCEL_MAX_COUNT;

        // サブピクセル蓄積
        subpx_x += sign_x * constrain(accel_dx, 0, cap_x);
        subpx_y += sign_y * constrain(accel_dy, 0, cap_y);
    } else {
        hold_counter = 0;
        subpx_x = 0;
        subpx_y = 0;
    }

    // 蓄積値から整数ピクセルを取り出す
    mouse_report.x = (int8_t)constrain(subpx_x / 100, -127, 127);
    mouse_report.y = (int8_t)constrain(subpx_y / 100, -127, 127);
    subpx_x -= mouse_report.x * 100;
    subpx_y -= mouse_report.y * 100;

#if JOYSTICK_DEBUG
    if (++debug_timer >= 100) {
        debug_timer = 0;
        uprintf("Xr=%u cx=%u dx=%d | Yr=%u cy=%u dy=%d | h=%u\n",
                smooth_x, center_x, mouse_report.x,
                smooth_y, center_y, mouse_report.y,
                hold_counter);
    }
#endif

    return mouse_report;
}

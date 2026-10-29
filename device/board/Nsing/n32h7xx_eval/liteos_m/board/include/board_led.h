/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file board_led.h
 * @brief Tri-color user LEDs (common-anode, LOW = ON).
 *
 * Kept free of the N32H7xx device header on purpose: files that also include
 * the LiteOS-M headers (los_config.h -> target_config.h -> soc.h) would
 * otherwise see two conflicting definitions of enum IRQn.
 */

#ifndef BOARD_LED_H
#define BOARD_LED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_LED_RED = 0,
    BOARD_LED_GREEN = 1,
    BOARD_LED_BLUE = 2,
    BOARD_LED_COUNT
} BoardLedId;

void BoardLedEarlyOn(void);
void BoardLedInit(void);
void BoardLedSet(BoardLedId id, bool on);
void BoardLedToggle(BoardLedId id);
void BoardLedAllOff(void);

/* ---------------------------------------------------------------------------
 * The same LEDs as HDF logical GPIO numbers, i.e. what an application passes to
 * GpioSetDir()/GpioWrite().
 *
 * gpio_n32h7xx.c registers a single GPIO controller with start = 0, count = 144
 * and the flat numbering "group * 16 + pin", so the wiring is written down once
 * here instead of being recomputed by every application:
 *
 *   RED   PB3  -> group 1 * 16 + 3  = 19
 *   GREEN PF10 -> group 5 * 16 + 10 = 90
 *   BLUE  PI8  -> group 8 * 16 + 8  = 136
 *
 * The LEDs are wired common-anode, so driving the pin LOW lights them.
 * ------------------------------------------------------------------------- */
#define BOARD_LED_HDF_GPIO_RED      ((uint16_t)(1 * 16 + 3))
#define BOARD_LED_HDF_GPIO_GREEN    ((uint16_t)(5 * 16 + 10))
#define BOARD_LED_HDF_GPIO_BLUE     ((uint16_t)(8 * 16 + 8))

#define BOARD_LED_HDF_LEVEL_ON      0U
#define BOARD_LED_HDF_LEVEL_OFF     1U

#ifdef __cplusplus
}
#endif

#endif /* BOARD_LED_H */

/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file n32_adc_hw.h
 * @brief Minimal ADC hardware shim for the on-chip temperature sensor.
 *
 * Unlike the I2C and PWM shims, this one is not driven by an HDF driver: there
 * is no HDF ADC platform driver in this port yet, so the application calls
 * these functions directly. That is a deliberate exception for the demo, and it
 * is the only place in the project where an application reaches the SoC layer.
 *
 * WHY THE SHIM STILL EXISTS: the application must not include the vendor device
 * header (soc.h and n32h7xx.h both declare `enum IRQn`, neither guarded).
 *
 * RESOURCES - taken from the SDK ADC_TEMP example, which is verified on this
 * board:
 *   ADC3, internal temperature sensor = channel 18
 *   sample time 397 cycles, 12-bit right aligned, software trigger
 *   VTS calibration word read from OTP at 0x03B0 (fallback 1342)
 *
 * Note: the temperature sensor needs no pin, so - unlike GPIO / I2C / PWM -
 * there is no pre-kernel pin-mux step.
 */
#ifndef N32_ADC_HW_H
#define N32_ADC_HW_H

#include <stdint.h>

#define N32_ADC_OK              0
#define N32_ADC_ERR             (-1)
#define N32_ADC_TIMEOUT         (-2)

/**
 * @brief Enable clocks, configure ADC3 for the internal temperature sensor and
 *        run the calibration sequence.
 *
 * Safe to call after the kernel is up: no vendor GPIO helper is involved.
 *
 * @return N32_ADC_OK on success, N32_ADC_TIMEOUT if a status flag never
 *         asserted, N32_ADC_ERR on a parameter problem.
 */
int32_t N32AdcHwInit(void);

/**
 * @brief Trigger one conversion and return the raw 12-bit code.
 * @return N32_ADC_OK on success, N32_ADC_TIMEOUT if ENDC never asserted.
 */
int32_t N32AdcHwReadRaw(uint16_t *raw);

/**
 * @brief Read the temperature in tenths of a degree Celsius.
 *        Example: 423 means 42.3 C.
 *
 * Conversion follows the SDK ADC_TEMP example:
 *   mV     = code * 3300 / 4095
 *   temp_C = (VTS - mV) / 3.4 + 30 - 1.25
 * where VTS is the per-die calibration word from OTP.
 */
int32_t N32AdcHwReadTemp(int32_t *deciCelsius);

/** @brief The VTS calibration word in use, for diagnostics. */
uint16_t N32AdcHwVts(void);

/** @brief The raw code of the most recent conversion, for diagnostics. */
uint16_t N32AdcHwLastRaw(void);

#endif /* N32_ADC_HW_H */

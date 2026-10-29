/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file n32_pwm_hw.h
 * @brief Minimal PWM hardware shim between the N32H7xx SDK and the HDF PWM
 *        driver.
 *
 * WHY THIS FILE EXISTS: an HDF driver cannot include the vendor device header.
 * The HDF header chain reaches los_config.h -> target_config.h -> soc.h, which
 * declares `enum IRQn`; n32h7xx.h declares the same enum and neither is
 * guarded. So all register-level work lives here.
 *
 * RESOURCES - taken from the SDK PWM_Output example, which is verified on this
 * board:
 *   timer  GTIMA1
 *   CH1 -> PA0 (AF6)      CH2 -> PA1 (AF4)
 *
 * UNITS: period and duty are exchanged in counter ticks. The prescaler is
 * computed so that the counter runs at N32_PWM_TICK_HZ, i.e. one tick is one
 * microsecond. The HDF layer converts nanoseconds to ticks and back.
 *
 * LIMITS: ARR is 16 bit, so the longest period is 0xFFFF ticks = 65.535 ms
 * (about 15 Hz). Below that the resolution is 1 us.
 */
#ifndef N32_PWM_HW_H
#define N32_PWM_HW_H

#include <stdint.h>

#define N32_PWM_CH_1            1U
#define N32_PWM_CH_2            2U

/*
 * Whether this driver owns PA1 as GTIMA1_CH2.
 *
 * On the N32H7xx EVAL board PA1 is ETH_REFCLK. Muxing it here would put a
 * push-pull timer output on the PHY clock line, so by default the pin is left
 * completely untouched and OC2 is not configured: the customer decides whether
 * PA1 serves Ethernet or PWM. Set to 1 to let this driver drive PA1 as PWM.
 */
#ifndef N32_PWM_USE_CH2
#define N32_PWM_USE_CH2         0
#endif

/*
 * Timer / pin selection for CH1.
 *   0 -> GTIMA1_CH1 on PA0 (AF6)   default, but PA0 is also an ETH pin on the
 *                                   EVAL board, so an external driver on that
 *                                   net will fight a push-pull output.
 *   1 -> GTIMA2_CH1 on PA6 (AF11)  PA6 is DVP PIXCLK on the EVAL board; the same
 *                                   caveat applies if a camera module is
 *                                   powered and driving PIXCLK.
 * Either way the pin is driven only when this driver is initialised.
 */
#ifndef N32_PWM_USE_PA6
#define N32_PWM_USE_PA6         1
#endif

/** @brief "GTIMA1/PA0" or "GTIMA2/PA6", for logs. */
const char *N32PwmHwName(void);

#define N32_PWM_OK              0
#define N32_PWM_ERR             (-1)

/** Counter clock after the prescaler: 1 MHz, so one tick is 1 us. */
#define N32_PWM_TICK_HZ         1000000U

/** ARR is 16 bit, a larger period cannot be represented. */
#define N32_PWM_MAX_TICKS       0xFFFFU

/** Timer input clock assumed when the hcs parameter is absent.
 *  RCC Mode0 drives the M7 at 600 MHz with APB2 at 150 MHz; the timer clock is
 *  APB2 x 2. The SDK example derives the same figure as SysClkFreq / 2. */
#define N32_PWM_TIMER_CLK_HZ    300000000U

/**
 * @brief Route PA0 to GTIMA1_CH1. PA1 is only touched when N32_PWM_USE_CH2.
 *
 *
 * MUST be called before the kernel starts. The SDK GPIO helper overruns its
 * destination when called from an HDF driver Init(), i.e. from task context
 * after the kernel is up - the same constraint documented in n32_i2c_hw.h.
 */
int32_t N32PwmHwPinInit(void);

/**
 * @brief Configure the timer time base and both output channels.
 *
 * @param timerClkHz Timer input clock in Hz. Must divide evenly by
 *                   N32_PWM_TICK_HZ so that a whole-number prescaler exists.
 * @return N32_PWM_OK on success, N32_PWM_ERR when the clock cannot be divided.
 */
int32_t N32PwmHwInit(uint32_t timerClkHz);

/** @brief Set the auto-reload value, i.e. the period in ticks. */
int32_t N32PwmHwSetPeriod(uint32_t ticks);

/** @brief Set the compare value of one channel, i.e. the high time in ticks. */
int32_t N32PwmHwSetDuty(uint8_t ch, uint32_t ticks);

/** @brief Start the counter. Outputs keep their configured compare values. */
int32_t N32PwmHwStart(void);

/** @brief Stop the counter. Both outputs freeze at their inactive level. */
int32_t N32PwmHwStop(void);

/** @brief Prescaler actually programmed, for diagnostics. */
uint32_t N32PwmHwPrescaler(void);

/**
 * @brief Take a register snapshot for diagnostics:
 *        AR, PSC, CCDAT1, CCDAT2 and CCEN.
 */
#endif /* N32_PWM_HW_H */

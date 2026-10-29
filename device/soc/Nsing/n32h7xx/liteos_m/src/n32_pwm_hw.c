/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file n32_pwm_hw.c
 * @brief Register-level PWM implementation for N32H7xx. See n32_pwm_hw.h for
 *        why this shim exists and for the pin / timer assignment.
 *
 * The sequence matches the SDK PWM_Output example: enable the timer clock,
 * program the time base, configure each channel in PWM1 mode with preload
 * enabled, enable ARR preload, then start the counter. The only deviation is
 * the prescaler target: the example aims at 12 MHz, this shim aims at 1 MHz so
 * that one tick equals one microsecond and nanosecond periods from the HDF
 * layer convert with a single division.
 */

#include "n32_pwm_hw.h"

#include "n32h7xx_tim.h"
#include "n32h7xx_rcc.h"
#include "n32h7xx_gpio.h"

static uint32_t g_prescaler = 0U;
static uint32_t g_ready = 0U;

const char *N32PwmHwName(void)
{
#if N32_PWM_USE_PA6
    return "GTIMA2/PA6";
#else
    return "GTIMA1/PA0";
#endif
}

static TIM_Module *N32PwmBase(void)
{
#if N32_PWM_USE_PA6
    return GTIMA2;
#else
    return GTIMA1;
#endif
}

int32_t N32PwmHwPinInit(void)
{
    GPIO_InitType init;

    /* Both the M7 and M4 enable bits, as board_config.h does for the LEDs. */
    RCC_EnableAHB5PeriphClk1(RCC_AHB5_PERIPHEN_M7_GPIOA | RCC_AHB5_PERIPHEN_M4_GPIOA, ENABLE);
    RCC_EnableAHB5PeriphClk2(RCC_AHB5_PERIPHEN_PWR | RCC_AHB5_PERIPHEN_M7_AFIO, ENABLE);

    GPIO_InitStruct(&init);
    init.GPIO_Mode = GPIO_MODE_AF_PP;       /* push-pull: a PWM output drives the pin */

#if N32_PWM_USE_PA6
    init.Pin            = GPIO_PIN_6;       /* GTIMA2_CH1 */
    init.GPIO_Alternate = GPIO_AF11;
#else
    init.Pin            = GPIO_PIN_0;       /* GTIMA1_CH1 */
    init.GPIO_Alternate = GPIO_AF6;
#endif
    GPIO_InitPeripheral(GPIOA, &init);

#if N32_PWM_USE_CH2
#if N32_PWM_USE_CH2
#if N32_PWM_USE_CH2
    init.Pin            = GPIO_PIN_1;       /* GTIMA1_CH2 */
    init.GPIO_Alternate = GPIO_AF4;
    GPIO_InitPeripheral(GPIOA, &init);
#endif
#else
    /* PA1 intentionally left untouched: it is ETH_REFCLK on this board. */
#endif
#else
    /* PA1 intentionally left untouched: it is ETH_REFCLK on this board. */
#endif

    return N32_PWM_OK;
}

int32_t N32PwmHwInit(uint32_t timerClkHz)
{
    TIM_TimeBaseInitType tb;
    OCInitType oc;

    if (timerClkHz < N32_PWM_TICK_HZ || (timerClkHz % N32_PWM_TICK_HZ) != 0U) {
        return N32_PWM_ERR;                 /* no whole-number prescaler exists */
    }

#if N32_PWM_USE_PA6
    RCC_EnableAPB2PeriphClk1(RCC_APB2_PERIPHEN_M7_GTIMA2 | RCC_APB2_PERIPHEN_M7_GTIMA2LP, ENABLE);
#else
    RCC_EnableAPB2PeriphClk1(RCC_APB2_PERIPHEN_M7_GTIMA1 | RCC_APB2_PERIPHEN_M7_GTIMA1LP, ENABLE);
#endif

    g_prescaler = (timerClkHz / N32_PWM_TICK_HZ) - 1U;

    TIM_InitTimBaseStruct(&tb);
    tb.Period      = 1000U;                 /* 1 kHz placeholder, replaced by SetPeriod */
    tb.Prescaler   = g_prescaler;
    tb.ClkDiv      = TIM_CLK_DIV1;
    tb.CounterMode = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(N32PwmBase(), &tb);

    TIM_InitOcStruct(&oc);
    oc.OCMode      = TIM_OCMODE_PWM1;
    oc.OutputState = TIM_OUTPUT_STATE_ENABLE;
    oc.OCPolarity  = TIM_OC_POLARITY_HIGH;
    oc.Pulse       = 0U;                    /* start with 0 % high time */

    TIM_InitOc1(N32PwmBase(), &oc);
    TIM_ConfigOc1Preload(N32PwmBase(), TIM_OC_PRE_LOAD_ENABLE);

#if N32_PWM_USE_CH2
#if N32_PWM_USE_CH2
    TIM_InitOc2(N32PwmBase(), &oc);
    TIM_ConfigOc2Preload(N32PwmBase(), TIM_OC_PRE_LOAD_ENABLE);
#else
    /* OC2 stays disabled with PA1 unconfigured, so the timer cannot drive the
     * PHY reference clock through an OCR output. */
#endif
#else
    /* OC2 stays disabled with PA1 unconfigured, so the timer cannot drive the
     * PHY reference clock through an OCR output. */
#endif

    TIM_ConfigArPreload(N32PwmBase(), ENABLE);

    TIM_GenerateEvent(N32PwmBase(), TIM_EVT_SRC_UPDATE);
    g_ready = 1U;
    return N32_PWM_OK;
}

int32_t N32PwmHwSetPeriod(uint32_t ticks)
{
    if (ticks == 0U || ticks > N32_PWM_MAX_TICKS) {
        return N32_PWM_ERR;
    }
    N32PwmBase()->AR = ticks;

    /* AR preload is on: the value only reaches the active register on an update
     * event. Runtime period changes would otherwise wait for a natural overflow. */
    TIM_GenerateEvent(N32PwmBase(), TIM_EVT_SRC_UPDATE);
    return N32_PWM_OK;
}

int32_t N32PwmHwSetDuty(uint8_t ch, uint32_t ticks)
{
    if (ticks > N32_PWM_MAX_TICKS) {
        return N32_PWM_ERR;
    }
    if (ch == N32_PWM_CH_1) {
        N32PwmBase()->CCDAT1 = ticks;
    } else if (ch == N32_PWM_CH_2) {
        N32PwmBase()->CCDAT2 = ticks;
    } else {
        return N32_PWM_ERR;
    }
    /* CCR preload: push it to the active register now. */
    TIM_GenerateEvent(N32PwmBase(), TIM_EVT_SRC_UPDATE);
    return N32_PWM_OK;
}

int32_t N32PwmHwStart(void)
{
    if (g_ready == 0U) {
        return N32_PWM_ERR;
    }
    TIM_Enable(N32PwmBase(), ENABLE);
    return N32_PWM_OK;
}

int32_t N32PwmHwStop(void)
{
    if (g_ready == 0U) {
        return N32_PWM_ERR;
    }
    TIM_Enable(N32PwmBase(), DISABLE);
    return N32_PWM_OK;
}

uint32_t N32PwmHwPrescaler(void)
{
    return g_prescaler;
}


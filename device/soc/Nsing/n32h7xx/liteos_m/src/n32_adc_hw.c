/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file n32_adc_hw.c
 * @brief Register-level ADC implementation for the on-chip temperature sensor.
 *        See n32_adc_hw.h for why this shim exists and why it is called
 *        directly from the application.
 *
 * The sequence follows the SDK ADC_TEMP example, with one deliberate change:
 * the example streams results into memory with DMA, this shim converts on
 * demand and reads ADC3->DAT. Polling is sufficient for a 1 Hz temperature
 * reading and removes the DMA / DMAMUX configuration entirely.
 */

#include "n32_adc_hw.h"

#include "n32h7xx_adc.h"
#include "n32h7xx_rcc.h"
#include "n32h7xx_otpc.h"

/* Loop bound for every status poll. The calibration and a 397-cycle
 * conversion both complete far below this at 600 MHz, so hitting it means the
 * peripheral is not clocked. */
/* Bounded wait for one conversion. Kept deliberately short: this loop runs in
 * a task context, so a large bound starves every lower-priority task. At a few
 * tens of cycles per iteration this is well under a millisecond. */
#define N32_ADC_SPIN_LIMIT      20000U

#define N32_ADC_VREF_MV         3300U
#define N32_ADC_FULL_SCALE      4095U

/* Per-die temperature-sensor calibration word, see the SDK example. */
#define N32_ADC_OTP_VTS_ADDR    0x03B0U
#define N32_ADC_VTS_DEFAULT     1342U

/* Sensor slope and offset from the datasheet / SDK example. */
#define N32_ADC_SLOPE_MV_PER_C  3.4f
#define N32_ADC_OFFSET_C        30.0f
#define N32_ADC_TRIM_C          1.25f

static uint16_t g_vts = N32_ADC_VTS_DEFAULT;
static uint16_t g_lastRaw = 0U;
static uint8_t  g_ready = 0U;

static void N32AdcClockInit(void)
{
    RCC_EnableAHB5PeriphClk2(RCC_AHB5_PERIPHEN_M7_AFIO, ENABLE);

    /* OTP holds the temperature-sensor calibration word. */
    RCC_EnableAXIPeriphReset1(RCC_AXI_PERIPHRST_OTPC);
    RCC_EnableAXIPeriphClk1(RCC_AXI_PERIPHEN_M7_OTPC | RCC_AXI_PERIPHEN_M4_OTPC, ENABLE);

    /* ADC1 carries the temperature-sensor enable bit (ADC1->CTRL2 TS_EN), so
     * its clock must be on even though the conversion runs on ADC3. With ADC1
     * clocked off the write below is silently dropped: the sensor stays off,
     * channel 18 measures ~0 V and the formula reports several hundred degrees.
     * The SDK ADC_TEMP example enables both for the same reason. */
    RCC_EnableAHB1PeriphClk1(RCC_AHB1_PERIPHEN_M7_ADC1SYS | RCC_AHB1_PERIPHEN_M7_ADC1BUS, ENABLE);
    RCC_EnableAHB1PeriphClk4(RCC_AHB1_PERIPHEN_M7_ADC3SYS | RCC_AHB1_PERIPHEN_M7_ADC3BUS, ENABLE);
}

static void N32AdcLoadVts(void)
{
    uint32_t value = 0U;
    uint16_t vts;

    /* A failed read leaves value untouched (0). Treat 0 the same as 0xFFFF:
     * both mean "no calibration", and using 0 would push the temperature
     * formula far off (VTS is roughly 1.3 V). */
    (void)OTPC_ReadWord(N32_ADC_OTP_VTS_ADDR, &value);
    vts = (uint16_t)(value & 0xFFFFU);
    if (vts != 0U && vts != 0xFFFFU) {
        g_vts = vts;
    }
}

int32_t N32AdcHwInit(void)
{
    ADC_InitType init;
    uint32_t     spin;

    N32AdcClockInit();

    /* ADC kernel clock: AHB source, PLL1B, divided by 15 - as the SDK example. */
    ADC_ConfigClk(ADC3, ADC_CTRL3_CKMOD_AHB, RCC_ADCPLLCLK_SRC_PLL1B, RCC_ADCSYSCLK_DIV15);

    ADC_InitStruct(&init);
    init.WorkMode         = ADC_WORKMODE_INDEPENDENT;
    init.MultiChEn        = DISABLE;
    init.ContinueConvEn   = ENABLE;             /* free-running, as the SDK example */
    init.ExtTrigSelect    = ADC_EXT_TRIG_REG_CONV_SOFTWARE;
    init.DataTransferMode = ADC_REG_DR_TRANSFER; /* result into ADC3->DAT, no DMA */
    init.DatAlign         = ADC_DAT_ALIGN_R;
    init.ChsNumber        = 1;
    init.Resolution       = ADC_DATA_RES_12BIT;
    ADC_Init(ADC3, &init);

    ADC_Enable(ADC3, ENABLE);
    ADC_EnableTempSensor(ENABLE);

    /* Let the sensor settle before the first conversion. */
    for (volatile uint32_t settle = 0U; settle < 20000U; settle++) {
    }
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_18_Temperture_Sensor, 1,
                             ADC_SAMP_TIME_CYCLES_397);

    spin = 0U;
    while (ADC_GetFlagStatus(ADC3, ADC_FLAG_RDY) == RESET) {
        if (++spin > N32_ADC_SPIN_LIMIT) {
            return N32_ADC_TIMEOUT;
        }
    }

    ADC_CalibrationOperation(ADC3);
    spin = 0U;
    while (ADC_GetCalibrationStatus(ADC3) != RESET) {
        if (++spin > N32_ADC_SPIN_LIMIT) {
            return N32_ADC_TIMEOUT;
        }
    }

    /* Arm the sequence and trigger it once. With continuous conversion the
     * peripheral keeps producing samples, so a read never has to re-trigger.
     * Triggering per read was the earlier design and it failed from the second
     * read on - the sequence was not re-armed, ENDC never asserted again. */
    ADC_StartRegularConv(ADC3);
    ADC_EnableSoftwareStartConv(ADC3, ENABLE);

    N32AdcLoadVts();
    g_ready = 1U;
    return N32_ADC_OK;
}

int32_t N32AdcHwReadRaw(uint16_t *raw)
{
    uint32_t spin;

    if (g_ready == 0U || raw == NULL) {
        return N32_ADC_ERR;
    }

    spin = 0U;
    while (ADC_GetFlagStatus(ADC3, ADC_FLAG_ENDC) == RESET) {
        if (++spin > N32_ADC_SPIN_LIMIT) {
            return N32_ADC_TIMEOUT;
        }
    }

    *raw = (uint16_t)(ADC3->DAT & 0xFFFFU);
    g_lastRaw = *raw;
    ADC_ClearFlag(ADC3, ADC_FLAG_ENDC);
    return N32_ADC_OK;
}

int32_t N32AdcHwReadTemp(int32_t *deciCelsius)
{
    uint16_t raw = 0U;
    float    milliVolt;
    float    celsius;
    int32_t  ret;

    if (deciCelsius == NULL) {
        return N32_ADC_ERR;
    }

    ret = N32AdcHwReadRaw(&raw);
    if (ret != N32_ADC_OK) {
        return ret;
    }

    milliVolt = (float)raw * (float)N32_ADC_VREF_MV / (float)N32_ADC_FULL_SCALE;
    celsius   = ((float)g_vts - milliVolt) / N32_ADC_SLOPE_MV_PER_C
                + N32_ADC_OFFSET_C - N32_ADC_TRIM_C;

    /* Round to the nearest 0.1 C so the printed value does not jitter. */
    *deciCelsius = (int32_t)(celsius * 10.0f + ((celsius >= 0.0f) ? 0.5f : -0.5f));
    return N32_ADC_OK;
}

uint16_t N32AdcHwVts(void)
{
    return g_vts;
}

uint16_t N32AdcHwLastRaw(void)
{
    return g_lastRaw;
}

/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file pwm_n32h7xx.c
 * @brief HDF platform PWM driver for N32H7xx (LiteOS-M / Cortex-M7).
 *
 * Unlike I2C, the PWM framework needs NO manager node: PwmDeviceAdd() wires
 * device->service itself. What it does need is a service name that matches the
 * one PwmOpen() looks up, which is built as "HDF_PLATFORM_PWM_<num>" inside
 * pwm_if.c:PwmGetDevByNum(). The hcs node must therefore use policy = 2 and
 * serviceName = "HDF_PLATFORM_PWM_0" (or _1).
 *
 * One device node drives one timer channel. Two nodes are configured in this
 * product: num 0 -> GTIMA1_CH1 (PA0), num 1 -> GTIMA1_CH2 (PA1). Both channels
 * share the timer, so the period is common; only the duty cycle differs.
 *
 * This file deliberately includes NO vendor device header - see n32_pwm_hw.h.
 */

#include "device_resource_if.h"
#include "hdf_device_desc.h"
#include "hdf_core_log.h"
#include "pwm_core.h"
#include "osal_mem.h"

#include "n32_pwm_hw.h"

#define HDF_LOG_TAG pwm_n32h7xx

/*
 * PWM_DIAG writes straight to the USART1 registers, bypassing hilog. hilog is
 * initialised after DeviceManagerStart(), so HDF_LOGI() from Init() would be
 * lost exactly when it matters most.
 */
#define PWM_DIAG 1

#if PWM_DIAG
#define DIAG_USART_BASE     0x4000C400UL
#define DIAG_STS_OFFSET     0x0CUL
#define DIAG_DAT_OFFSET     0x10UL
#define DIAG_STS_TXDE       0x00000080UL
#define DIAG_REG32(addr)    (*(volatile uint32_t *)(addr))

static void DiagPuts(const char *s)
{
    while (*s != '\0') {
        while ((DIAG_REG32(DIAG_USART_BASE + DIAG_STS_OFFSET) & DIAG_STS_TXDE) == 0U) {
        }
        DIAG_REG32(DIAG_USART_BASE + DIAG_DAT_OFFSET) = (uint8_t)(*s++);
    }
}

static void DiagPutDec(uint32_t v)
{
    char    buf[12];
    int32_t i = 0;

    if (v == 0U) {
        DiagPuts("0");
        return;
    }
    while (v > 0U && i < 11) {
        buf[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (i > 0) {
        char c[2];
        c[0] = buf[--i];
        c[1] = '\0';
        DiagPuts(c);
    }
}
#else
#define DiagPuts(s) do { } while (0)
#define DiagPutDec(v) do { } while (0)
#endif

struct N32PwmRes {
    uint32_t num;           /* PWM device number, must match the service name */
    uint8_t  ch;            /* N32_PWM_CH_1 / N32_PWM_CH_2 */
    uint32_t timerClkHz;    /* timer input clock, see N32_PWM_TIMER_CLK_HZ */
};

/*
 * Both logical channels share one timer, so the counter and the period register
 * are global. Two things must therefore be tracked here instead of per device:
 *   - which channels are enabled, so stopping one does not stop the other;
 *   - the period currently in effect, so a second channel cannot silently
 *     re-period a channel that is already running.
 */
#define N32_PWM_CHANNEL_COUNT   2U
static uint8_t  g_chOn[N32_PWM_CHANNEL_COUNT];
static uint32_t g_chUsers;
static uint32_t g_periodInUse;

static struct PwmMethod g_n32PwmMethod;

/* The timer and its two channels are configured once, on the first Init(). */
static uint8_t g_hwReady = 0U;

static int32_t N32PwmSetConfig(struct PwmDev *pwm, struct PwmConfig *config);
static int32_t N32PwmOpen(struct PwmDev *pwm);
static int32_t N32PwmClose(struct PwmDev *pwm);

static int32_t N32PwmBind(struct HdfDeviceObject *device)
{
    struct PwmDev *pwm = NULL;

    if (device == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }

    pwm = (struct PwmDev *)OsalMemCalloc(sizeof(struct PwmDev));
    if (pwm == NULL) {
        return HDF_ERR_MALLOC_FAIL;
    }

    device->service = &pwm->service;
    pwm->device = device;
    return HDF_SUCCESS;
}

/**
 * @brief Read the parameter node, falling back to board defaults.
 *
 * The node arrives from HdfDeviceNodeConstruct(), which looks it up by
 * deviceMatchAttr - so the values live in hdf_config/pwm/pwm_config.hcs.
 */
static int32_t N32PwmGetResource(struct N32PwmRes *res, const struct DeviceResourceNode *node)
{
    struct DeviceResourceIface *dri = NULL;

    res->num         = 0U;
    res->ch          = N32_PWM_CH_1;
    res->timerClkHz  = N32_PWM_TIMER_CLK_HZ;

    if (node == NULL) {
        DiagPuts("[PWM] no hcs node -> defaults (num 0, ch1)\r\n");
        return HDF_SUCCESS;
    }

    dri = DeviceResourceGetIfaceInstance(HDF_CONFIG_SOURCE);
    if (dri == NULL || dri->GetUint8 == NULL || dri->GetUint32 == NULL) {
        DiagPuts("[PWM] no resource iface -> defaults\r\n");
        return HDF_SUCCESS;
    }

    (void)dri->GetUint32(node, "num", &res->num, 0);
    (void)dri->GetUint8(node, "channel", &res->ch, 0);
    (void)dri->GetUint32(node, "timerClkHz", &res->timerClkHz, N32_PWM_TIMER_CLK_HZ);

    if (res->ch != N32_PWM_CH_1 && res->ch != N32_PWM_CH_2) {
        DiagPuts("[PWM] channel out of range -> forced to CH1\r\n");
        res->ch = N32_PWM_CH_1;
    }
    return HDF_SUCCESS;
}

static int32_t N32PwmInit(struct HdfDeviceObject *device)
{
    struct PwmDev   *pwm = NULL;
    struct N32PwmRes *res = NULL;
    int32_t          ret;

    if (device == NULL || device->service == NULL) {
        return HDF_ERR_INVALID_OBJECT;
    }
    pwm = (struct PwmDev *)device->service;

    res = (struct N32PwmRes *)OsalMemCalloc(sizeof(struct N32PwmRes));
    if (res == NULL) {
        return HDF_ERR_MALLOC_FAIL;
    }
    ret = N32PwmGetResource(res, device->property);
    if (ret != HDF_SUCCESS) {
        OsalMemFree(res);
        return ret;
    }

    if (g_hwReady == 0U) {
        if (N32PwmHwInit(res->timerClkHz) != N32_PWM_OK) {
            DiagPuts("[PWM] ERR: timer clock not divisible\r\n");
            OsalMemFree(res);
            return HDF_FAILURE;
        }
        g_hwReady = 1U;
        DiagPuts("[PWM] timer ready: ");
        DiagPuts(N32PwmHwName());
        DiagPuts(", prescaler ");
        DiagPutDec(N32PwmHwPrescaler());
        DiagPuts(", tick 1us\r\n");
    }

    g_n32PwmMethod.setConfig = N32PwmSetConfig;
    g_n32PwmMethod.open      = N32PwmOpen;
    g_n32PwmMethod.close     = N32PwmClose;

    pwm->num    = res->num;
    pwm->method = &g_n32PwmMethod;
    (void)PwmSetPriv(pwm, res);

    ret = PwmDeviceAdd(device, pwm);
    if (ret != HDF_SUCCESS) {
        DiagPuts("[PWM] ERR: PwmDeviceAdd\r\n");
        OsalMemFree(res);
        return ret;
    }

    DiagPuts("[PWM] node ");
    DiagPutDec(res->num);
    DiagPuts(" registered, channel ");
    DiagPutDec(res->ch);
    DiagPuts("\r\n");
    return HDF_SUCCESS;
}

static void N32PwmRelease(struct HdfDeviceObject *device)
{
    struct PwmDev *pwm = NULL;

    if (device == NULL || device->service == NULL) {
        return;
    }
    pwm = (struct PwmDev *)device->service;
    PwmDeviceRemove(device, pwm);
    if (pwm->priv != NULL) {
        OsalMemFree(pwm->priv);
        pwm->priv = NULL;
    }
    OsalMemFree(pwm);
}

/**
 * @brief Apply one configuration.
 *
 * The framework exchanges time in nanoseconds; this driver converts to counter
 * ticks (1 tick = 1 us) and programs ARR / CCDATn. The timer is shared by both
 * channels, so the period is common and only the duty cycle is per-channel.
 */
static int32_t N32PwmSetConfig(struct PwmDev *pwm, struct PwmConfig *config)
{
    struct N32PwmRes *res = NULL;
    uint32_t          periodTicks;
    uint32_t          dutyTicks;
    uint8_t           idx;

    if (pwm == NULL || config == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }
    res = (struct N32PwmRes *)PwmGetPriv(pwm);
    if (res == NULL) {
        return HDF_ERR_INVALID_OBJECT;
    }

    /* ns -> us; anything below 1 us collapses to one tick. */
    periodTicks = config->period / 1000U;
    dutyTicks   = config->duty / 1000U;

    if (periodTicks == 0U || periodTicks > N32_PWM_MAX_TICKS) {
        return HDF_ERR_INVALID_PARAM;
    }
    if (dutyTicks > periodTicks) {
        dutyTicks = periodTicks;
    }

    if (res->ch == N32_PWM_CH_2 && N32_PWM_USE_CH2 == 0) {
        /* The node exists in hcs, but CH2 is not wired on this board: report it
         * instead of accepting a configuration that produces no waveform. */
        HDF_LOGE("PWM CH2 disabled: PA1 is ETH_REFCLK (see N32_PWM_USE_CH2)");
        return HDF_ERR_NOT_SUPPORT;
    }

    if (res->ch == N32_PWM_CH_2 && N32_PWM_USE_CH2 == 0) {
        /* The node exists in hcs, but CH2 is not wired on this board: report it
         * instead of accepting a configuration that produces no waveform. */
        HDF_LOGE("PWM CH2 disabled: PA1 is ETH_REFCLK (see N32_PWM_USE_CH2)");
        return HDF_ERR_NOT_SUPPORT;
    }

    idx = (res->ch == N32_PWM_CH_2) ? 1U : 0U;

    if (config->status == PWM_ENABLE_STATUS) {
        /* One timer, one period: refuse to change it while another channel is
         * running, rather than changing that channel's waveform behind its back. */
        if (g_chUsers > 0U && periodTicks != g_periodInUse) {
            return HDF_ERR_NOT_SUPPORT;
        }
        if (N32PwmHwSetPeriod(periodTicks) != N32_PWM_OK ||
            N32PwmHwSetDuty(res->ch, dutyTicks) != N32_PWM_OK ||
            N32PwmHwStart() != N32_PWM_OK) {
            return HDF_FAILURE;
        }
        if (g_chOn[idx] == 0U) {
            g_chOn[idx] = 1U;
            g_chUsers++;
        }
        g_periodInUse = periodTicks;
    } else {
        (void)N32PwmHwSetDuty(res->ch, 0U);
        if (g_chOn[idx] != 0U) {
            g_chOn[idx] = 0U;
            if (g_chUsers > 0U) {
                g_chUsers--;
            }
        }
        /* Only the last channel may stop the time base. */
        if (g_chUsers == 0U) {
            (void)N32PwmHwStop();
            g_periodInUse = 0U;
        }
    }

    pwm->cfg = *config;
    return HDF_SUCCESS;
}

static int32_t N32PwmOpen(struct PwmDev *pwm)
{
    return (pwm == NULL) ? HDF_ERR_INVALID_PARAM : HDF_SUCCESS;
}

static int32_t N32PwmClose(struct PwmDev *pwm)
{
    return (pwm == NULL) ? HDF_ERR_INVALID_PARAM : HDF_SUCCESS;
}

struct HdfDriverEntry g_N32PwmDriverEntry = {
    .moduleVersion = 1,
    .moduleName    = "NSING_PWM_MODULE_HDF",
    .Bind          = N32PwmBind,
    .Init          = N32PwmInit,
    .Release       = N32PwmRelease,
};
HDF_INIT(g_N32PwmDriverEntry);

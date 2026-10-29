/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file app_main.c
 * @brief Sample application of the N32H7xx EVAL board.
 *
 * Two tasks, both driven purely through the HDF platform interfaces:
 *
 *   LED blinker  -> GpioSetDir()/GpioWrite()  -> HDF GPIO (NSING_GPIO_MODULE_HDF)
 *   heartbeat    -> UartOpen()/UartWrite()    -> HDF UART (NSING_UART_MODULE_HDF)
 *
 * Nothing here touches a hardware register, and nothing here knows a port name
 * or a register layout: the board wiring (which pin drives which LED, and that
 * the LEDs are common-anode) comes from board_led.h, and the pin numbering
 * scheme belongs to the GPIO driver. That is the intended split - the board
 * answers "what the hardware is", the application answers "what to do with it".
 *
 * The application is not called from main(). It registers with SYS_RUN() and the
 * OpenHarmony start-up chain invokes it:
 *
 *   main() -> LOS_KernelInit() -> DeviceManagerStart() -> OHOS_SystemInit()
 *              -> MODULE_INIT(run) -> SYS_RUN(AppDemoEntry)   <-- here
 *              -> LOS_Start()
 */

#include <stdarg.h>
#include <stdio.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "gpio_if.h"
#include "uart_if.h"

#include "board_led.h"
#include "i2c_if.h"
#include "pwm_if.h"

/* Demo tasks are compiled out when the product is built for XTS
 * (--gn-args app_demo=false); see app/BUILD.gn. */
#ifndef APP_DEMO_TASKS
#define APP_DEMO_TASKS 1
#endif

static void AppLogf(const char *fmt, ...);

#define APP_TASK_STACK_SIZE     0x1000
#define APP_LED_PRIO            6
#define APP_LOG_PRIO            7

/* Delay values in milliseconds. osDelay() takes TICKS, so every call site goes
   through MsToTicks() - never pass a millisecond value to osDelay() directly. */
#define APP_LED_PERIOD_MS       400U
#define APP_LOG_PERIOD_MS       1000U

/* The three user LEDs, as HDF logical GPIO numbers (see board_led.h). */
static const uint16_t g_ledPins[] = {
    BOARD_LED_HDF_GPIO_RED,
    BOARD_LED_HDF_GPIO_GREEN,
    BOARD_LED_HDF_GPIO_BLUE,
};
#define LED_COUNT               (sizeof(g_ledPins) / sizeof(g_ledPins[0]))

static DevHandle g_uartHandle = NULL;

/**
 * @brief Convert milliseconds to CMSIS-RTOS2 ticks.
 *
 * osDelay()/osDelayUntil() are specified in ticks, while everything on the board
 * is easier to reason about in milliseconds. The tick rate is read back from the
 * kernel at run time, so this stays correct if LOSCFG_BASE_CORE_TICK_PER_SECOND
 * ever changes. Rounds up, so a delay is never shorter than requested.
 */
static uint32_t MsToTicks(uint32_t ms)
{
    uint32_t freq = osKernelGetTickFreq();      /* ticks per second */

    if (freq == 0U) {
        return ms;                              /* kernel not up yet */
    }
    return ((ms * freq) + 999U) / 1000U;
}

/* Minimal unsigned-to-decimal helper (no libc dependency in the app). */
static int32_t U32ToStr(uint32_t v, char *out)
{
    char tmp[12];
    int32_t n = 0;
    int32_t i;

    if (v == 0U) {
        out[0] = '0';
        return 1;
    }
    while (v > 0U && n < (int32_t)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    for (i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    return n;
}

static void UartPuts(const char *s)
{
    uint32_t len = 0;

    if (g_uartHandle == NULL || s == NULL) {
        return;
    }
    while (s[len] != '\0') {
        len++;
    }
    (void)UartWrite(g_uartHandle, (uint8_t *)s, len);
}

/* Print a 32-bit value as 0xXXXXXXXX (no libc dependency). */
static void UartPutHex(uint32_t v)
{
    static const char digits[] = "0123456789abcdef";
    char buf[11];
    uint32_t i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = digits[(v >> ((7 - i) * 4)) & 0xFU];
    }
    buf[10] = '\0';
    UartPuts(buf);
}

/* Walks the LEDs: light one, wait, release it, move on. */
static void *AppLedTask(const char *arg)
{
    uint32_t i = 0U;

    (void)arg;
    while (1) {
        (void)GpioWrite(g_ledPins[i], BOARD_LED_HDF_LEVEL_ON);
        osDelay(MsToTicks(APP_LED_PERIOD_MS));
        (void)GpioWrite(g_ledPins[i], BOARD_LED_HDF_LEVEL_OFF);
        i = (i + 1U) % (uint32_t)LED_COUNT;
    }
    return NULL;
}

/* Periodic heartbeat so the console shows the system is still alive. */
static void *AppLogTask(const char *arg)
{
    uint32_t count = 0U;

    (void)arg;
    while (1) {
        char num[12];
        int32_t n;

        UartPuts("[N32H7] app: heartbeat ");
        n = U32ToStr(count, num);
        (void)UartWrite(g_uartHandle, (uint8_t *)num, (uint32_t)n);
        UartPuts("\r\n");

        count++;
        osDelay(MsToTicks(APP_LOG_PERIOD_MS));
    }
    return NULL;
}

static void *AppCreateTask(osThreadFunc_t entry, osPriority_t prio, const char *name)
{
    osThreadAttr_t attr = {0};   /* tz_module / reserved must not be stack garbage */

    attr.name = name;
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = APP_TASK_STACK_SIZE;
    attr.priority = prio;

    return osThreadNew(entry, NULL, &attr);
}

/**
 * @brief Application entry, registered with SYS_RUN().
 *
 * Invoked by OHOS_SystemInit() -> MODULE_INIT(run), i.e. after
 * DeviceManagerStart() has already brought up every HDF driver.
 */
static void AppDemoEntry(void)
{
    uint32_t i;

    /* ---- UART: port 0 -> HDF_PLATFORM_UART_0 (the USART1 console) ---- */
    g_uartHandle = UartOpen(0);
    if (g_uartHandle == NULL) {
        return;                             /* nothing to log to */
    }
    UartPuts("[N32H7] app      : SYS_RUN entry ran, HDF UART open\r\n");
    /* Report the tick rate the kernel actually runs at: the board config and the
     * product kernel_configs can disagree, and only this value is authoritative. */
    AppLogf("[N32H7] app      : tick rate = %u Hz (%u ms/tick)\n",
            (uint32_t)osKernelGetTickFreq(),
            1000U / (uint32_t)osKernelGetTickFreq());

    /* ---- GPIO: LED pins as outputs, all off ---- */
    for (i = 0U; i < (uint32_t)LED_COUNT; i++) {
        int32_t ret = GpioSetDir(g_ledPins[i], GPIO_DIR_OUT);

        if (ret != HDF_SUCCESS) {
            UartPuts("[N32H7] app      : GpioSetDir failed, pin=");
            UartPutHex((uint32_t)g_ledPins[i]);
            UartPuts(" ret=");
            UartPutHex((uint32_t)ret);
            UartPuts("\r\n");
            return;
        }
        (void)GpioWrite(g_ledPins[i], BOARD_LED_HDF_LEVEL_OFF);
    }
    UartPuts("[N32H7] app      : HDF GPIO ready (3 LED pins)\r\n");

    /* ---- tasks ---- */
    if (AppCreateTask((osThreadFunc_t)AppLedTask, (osPriority_t)APP_LED_PRIO, "app_led") == NULL) {
        UartPuts("[N32H7] app      : app_led create failed\r\n");
    }
    if (AppCreateTask((osThreadFunc_t)AppLogTask, (osPriority_t)APP_LOG_PRIO, "app_log") == NULL) {
        UartPuts("[N32H7] app      : app_log create failed\r\n");
    }
    UartPuts("[N32H7] app      : tasks created, LED blinker + heartbeat running\r\n");
}

#if APP_DEMO_TASKS
SYS_RUN(AppDemoEntry);
#endif

/* ---------------------------------------------------------------------------
 * I2C EEPROM demo - the SDK I2C_EEPROM example, re-done through HDF.
 *
 * Runs on its own task so the boot path is never blocked, and talks to the bus
 * only through I2cOpen()/I2cTransfer(). Which pins, which alternate function and
 * which clock the controller uses is the driver's business; this task only knows
 * the device it wants to talk to, exactly like the SDK example knows 0xA0.
 * ------------------------------------------------------------------------- */
#define APP_I2C_PRIO        8

static void AppPutHex2(uint8_t v)
{
    static const char dig[] = "0123456789abcdef";
    char s[3];

    s[0] = dig[(v >> 4) & 0xFU];
    s[1] = dig[v & 0xFU];
    s[2] = '\0';
    UartPuts(s);
}

/* AT24C02 on the board's I2C1 header. 0x50 is the 7-bit address, i.e. the SDK
   example's EEPROM_BLOCK0_ADDRESS 0xA0 with the R/W bit shifted out - the HDF
   I2C interface and the SoC layer both speak the 7-bit form. */
#define APP_EEPROM_ADDR     0x50U
#define APP_EEPROM_LEN      8U          /* AT24C02 page size, so this is one page */

static void *AppI2cEepromTask(void *arg)
{
    DevHandle     handle;
    struct I2cMsg writeMsg;
    struct I2cMsg readMsgs[2];
    uint8_t       wbuf[APP_EEPROM_LEN + 1U];
    uint8_t       rd[APP_EEPROM_LEN];
    uint8_t       i;
    uint8_t       match;

    (void)arg;
    osDelay(100U);                          /* ~1 s: let the log task settle */

    UartPuts("\r\n[I2C] opening bus 1 ...\r\n");
    handle = I2cOpen(1);
    if (handle == NULL) {
        UartPuts("[I2C] I2cOpen(1) FAILED\r\n");
        return NULL;
    }

    /* ---- write 8 bytes at internal address 0x00 -------------------------
       Same transfer the SDK example performs: [addr|W] + word address + data. */
    UartPuts("[I2C] EEPROM 0x50: writing 8 bytes @0x00 ...\r\n");

    wbuf[0] = 0x00U;                        /* EEPROM internal word address */
    for (i = 0; i < APP_EEPROM_LEN; i++) {
        wbuf[1U + i] = (uint8_t)(0xA0U + i);
    }

    writeMsg.addr  = APP_EEPROM_ADDR;
    writeMsg.flags = 0U;                    /* write */
    writeMsg.len   = (uint16_t)(APP_EEPROM_LEN + 1U);
    writeMsg.buf   = wbuf;

    if (I2cTransfer(handle, &writeMsg, 1) <= 0) {
        UartPuts("[I2C] EEPROM write FAILED\r\n\r\n");
        I2cClose(handle);
        return NULL;
    }

    /* AT24 internal write cycle is 5 ms max. Express it in time, not ticks:
       a bare tick count silently changes meaning if the tick rate changes. */
    osDelay(MsToTicks(10));

    /* ---- read back: [addr|W] memAddr, then [addr|R] data ---- */
    readMsgs[0].addr  = APP_EEPROM_ADDR;
    readMsgs[0].flags = 0U;
    readMsgs[0].len   = 1U;
    readMsgs[0].buf   = wbuf;               /* wbuf[0] is still the mem address */
    readMsgs[1].addr  = APP_EEPROM_ADDR;
    readMsgs[1].flags = I2C_FLAG_READ;
    readMsgs[1].len   = APP_EEPROM_LEN;
    readMsgs[1].buf   = rd;

    if (I2cTransfer(handle, readMsgs, 2) <= 0) {
        UartPuts("[I2C] EEPROM read FAILED\r\n\r\n");
        I2cClose(handle);
        return NULL;
    }

    /* ---- compare ---- */
    UartPuts("[I2C] read back:");
    match = 1U;
    for (i = 0; i < APP_EEPROM_LEN; i++) {
        UartPuts(" 0x");
        AppPutHex2(rd[i]);
        if (rd[i] != wbuf[1U + i]) {
            match = 0U;
        }
    }
    UartPuts("\r\n");
    if (match != 0U) {
        UartPuts("[I2C] EEPROM r/w PASS\r\n\r\n");
    } else {
        UartPuts("[I2C] EEPROM r/w FAIL (data mismatch)\r\n\r\n");
    }

    I2cClose(handle);
    return NULL;
}

static void AppI2cEepromEntry(void)
{
    if (AppCreateTask((osThreadFunc_t)AppI2cEepromTask,
                      (osPriority_t)APP_I2C_PRIO, "app_i2c") == NULL) {
        UartPuts("[I2C] eeprom: task create FAILED\r\n");
    }
}
#if APP_DEMO_TASKS
SYS_RUN(AppI2cEepromEntry);
#endif

/* ---------------------------------------------------------------------------
 * PWM demo.
 *
 * Both channels share GTIMA1, so the period is common and only the duty cycle
 * differs. The sweep runs one channel up while the other runs down, which makes
 * the two outputs distinguishable on a scope without a second time base.
 * ------------------------------------------------------------------------- */
#define APP_PWM_PRIO            9
#define APP_PWM_PERIOD_NS       1000000U    /* 1 kHz carrier */

/* 1 = hold one fixed duty cycle, so the waveform is stable and measurable.
 * 0 = sweep 0..100 %, one step every APP_PWM_STEP_MS (a demo, not a test signal:
 *     the envelope keeps changing and 0 %/100 % are DC, which looks like gaps
 *     on a logic analyser). */
#define APP_PWM_STATIC          1
#define APP_PWM_STATIC_PCT      50U

#define APP_PWM_STEP_PCT        10U
#define APP_PWM_STEP_MS         300U

static void AppPwmReport(uint32_t ch1)
{
    AppLogf("[PWM] CH1=%u%%\r\n", ch1);
}

static void *AppPwmTask(void *arg)
{
    DevHandle         h1 = NULL;
    struct PwmConfig  cfg;
#if !APP_PWM_STATIC
    uint32_t          step;
#endif

    (void)arg;
    osDelay(MsToTicks(1500));               /* let the log task settle first */

    /* CH1 (PA0) only. PA1 is ETH_REFCLK on this board and is not initialised by
     * the driver at all (see N32_PWM_USE_CH2), so it cannot be driven here. */
    h1 = PwmOpen(0);
    if (h1 == NULL) {
        UartPuts("[PWM] PwmOpen(0) failed\r\n");
        return NULL;
    }

    cfg.period   = APP_PWM_PERIOD_NS;
    cfg.number   = 0;                       /* continuous */
    cfg.polarity = PWM_NORMAL_POLARITY;
    cfg.status   = PWM_ENABLE_STATUS;

#if APP_PWM_STATIC
    /* One fixed duty cycle: 1 kHz, 50 %, run forever. Nothing changes after
     * this point, so the pin can be measured without chasing a moving edge. */
    cfg.duty = APP_PWM_PERIOD_NS * APP_PWM_STATIC_PCT / 100U;
    if (PwmSetConfig(h1, &cfg) != HDF_SUCCESS) {
        UartPuts("[PWM] set CH1 failed\r\n");
        PwmClose(h1);
        return NULL;
    }
    AppLogf("[PWM] CH1=%u%%, 1 kHz fixed, continuous\r\n", APP_PWM_STATIC_PCT);

    while (1) {
        osDelay(MsToTicks(10000));          /* keep the handle open, change nothing */
    }
#else
    UartPuts("[PWM] opened: GTIMA1 CH1=PA0, sweep 0-100%% every 300 ms\r\n");
    while (1) {
        for (step = 0U; step <= 100U; step += APP_PWM_STEP_PCT) {
            cfg.duty = APP_PWM_PERIOD_NS * step / 100U;
            if (PwmSetConfig(h1, &cfg) != HDF_SUCCESS) {
                UartPuts("[PWM] set CH1 failed\r\n");
                break;
            }
            AppPwmReport(step);
            osDelay(MsToTicks(APP_PWM_STEP_MS));
        }
    }
#endif

    PwmClose(h1);
    return NULL;
}

static void AppPwmEntry(void)
{
    if (AppCreateTask((osThreadFunc_t)AppPwmTask,
                      (osPriority_t)APP_PWM_PRIO, "app_pwm") == NULL) {
        UartPuts("[PWM] task create FAILED\r\n");
    }
}
#if APP_DEMO_TASKS
SYS_RUN(AppPwmEntry);
#endif

/* ---------------------------------------------------------------------------
 * ADC internal temperature demo.
 *
 * There is no HDF ADC platform driver in this port yet, so this task calls the
 * SoC shim directly. It is the only place where the application reaches below
 * the platform interface, and it is declared locally on purpose: the app must
 * not include the vendor device header.
 *
 * Sequence and conversion constants follow the SDK ADC_TEMP example:
 *   ADC3, channel 18 (internal temperature sensor), 397-cycle sample time,
 *   12-bit right aligned, VTS calibration word read from OTP.
 * ------------------------------------------------------------------------- */
/* Lowest of all tasks: a sensor sample every second never needs to preempt
 * anything, and this keeps a stuck conversion from delaying the rest. */
#define APP_ADC_PRIO            5
#define APP_ADC_PERIOD_MS       1000U

extern int32_t  N32AdcHwInit(void);
extern int32_t  N32AdcHwReadTemp(int32_t *deciCelsius);
extern uint16_t N32AdcHwVts(void);
extern uint16_t N32AdcHwLastRaw(void);

static void *AppAdcTempTask(void *arg)
{
    int32_t ret;
    int32_t deci = 0;

    (void)arg;
    osDelay(MsToTicks(1600));               /* after the log and PWM tasks settle */

    UartPuts("\r\n[ADC] init ADC3 channel 18 (internal temperature sensor) ...\r\n");
    ret = N32AdcHwInit();
    if (ret != 0) {
        AppLogf("[ADC] init FAILED, ret=%d\r\n", ret);
        return NULL;
    }

    AppLogf("[ADC] VTS calibration = %u\r\n", (uint32_t)N32AdcHwVts());

    while (1) {
        ret = N32AdcHwReadTemp(&deci);
        if (ret != 0) {
            AppLogf("[ADC] conversion timeout, ret=%d\r\n", ret);
        } else {
        {
            int32_t whole = deci / 10;
            int32_t frac  = deci % 10;

            if (frac < 0) {
                frac = -frac;
            }
            AppLogf("[ADC] chip temperature = %s%d.%d C  (raw=%u)\r\n",
                    (deci < 0) ? "-" : "", (whole < 0) ? -whole : whole, frac,
                    (uint32_t)N32AdcHwLastRaw());
        }
        }
        osDelay(MsToTicks(APP_ADC_PERIOD_MS));
    }

    return NULL;
}

static void AppAdcTempEntry(void)
{
    if (AppCreateTask((osThreadFunc_t)AppAdcTempTask,
                      (osPriority_t)APP_ADC_PRIO, "app_adc") == NULL) {
        UartPuts("[ADC] task create FAILED\r\n");
    }
}
#if APP_DEMO_TASKS
SYS_RUN(AppAdcTempEntry);
#endif

/*
 * Formatted output for application tasks.
 *
 * vsnprintf resolves to the board's self-contained formatter (board_print.c),
 * not to musl's stdio: no heap allocation and no FILE buffer, so a log line
 * reaches the UART as soon as the call returns. Do not call this from an
 * interrupt or an exception handler.
 */
static void AppLogf(const char *fmt, ...)
{
    char    buf[128];
    va_list ap;
    int32_t n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    /* vsnprintf returns the length the output WOULD need, not what fitted.
     * Sending it verbatim would read past buf[] on long messages. */
    if (n > (int32_t)sizeof(buf) - 1) {
        n = (int32_t)sizeof(buf) - 1;
    }
    if (n > 0) {
        (void)UartWrite(g_uartHandle, (uint8_t *)buf, (uint32_t)n);
    }
}

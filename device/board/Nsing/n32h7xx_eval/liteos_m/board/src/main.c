/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file main.c
 * @brief N32H7xx EVAL board entry point for OpenHarmony LiteOS-M (mini).
 *
 * Start-up chain:
 *   Reset_Handler (SoC startup.s) -> SystemInit -> main (here)
 *     -> BoardClockInit      RCC Mode0 + release CM4
 *     -> BoardLedEarlyOn     LED2/PB3 on, proves main() is alive
 *     -> UartInit            early console, available before the kernel
 *     -> LOS_KernelInit      kernel pools, idle task, tick, HWI vector table
 *     -> DeviceManagerStart  HDF: parse the .hcs blob, launch every driver
 *     -> OHOS_SystemInit     walk the six .zInit layers (bsp/device/core/
 *                            sys_service/sys_feature/run) and call every entry
 *     -> LOS_Start           start the scheduler; never returns
 *
 * The application itself is NOT started from here: it registers with
 * SYS_RUN() and is pulled in by the MODULE_INIT(run) layer above.
 *
 * NOTE: this file must NOT include the N32H7xx device header (directly or via
 * board_config.h). los_config.h pulls in target_config.h -> soc.h, which defines
 * enum IRQn; the device header defines the same enum, so both in one translation
 * unit is a redeclaration error. LED access goes through board_led.h instead.
 *
 * Every build carries a distinct banner so a captured serial log can be matched
 * to the exact firmware image that produced it.
 */

#include "los_config.h"
#include "los_debug.h"
#include "uart_console.h"
#include "board_led.h"

/* HDF / OHOS bootstrap entry points. Declared locally on purpose: this file
   must not include the device header nor the HDF headers (IRQn redeclaration). */
extern int DeviceManagerStart(void);
extern void OHOS_SystemInit(void);

/* ---------------------------------------------------------------------------
 * The OHOS initcall layers, as laid out by board.ld. OHOS_SystemInit() walks
 * them in this exact order and calls every entry it finds, so reporting the
 * table below documents the real start-up sequence instead of assuming it.
 * ------------------------------------------------------------------------- */
typedef void (*BootInitCall)(void);

extern BootInitCall __zinitcall_bsp_start[];
extern BootInitCall __zinitcall_bsp_end[];
extern BootInitCall __zinitcall_device_start[];
extern BootInitCall __zinitcall_device_end[];
extern BootInitCall __zinitcall_core_start[];
extern BootInitCall __zinitcall_core_end[];
extern BootInitCall __zinitcall_sys_service_start[];
extern BootInitCall __zinitcall_sys_service_end[];
extern BootInitCall __zinitcall_sys_feature_start[];
extern BootInitCall __zinitcall_sys_feature_end[];
extern BootInitCall __zinitcall_run_start[];
extern BootInitCall __zinitcall_run_end[];
/* hctest registers RUN_TEST_SUITE's execution entry in the `test` layer.
   OHOS_SystemInit() does not walk that layer, so main() has to. */
extern BootInitCall __zinitcall_test_start[];
extern BootInitCall __zinitcall_test_end[];

/* When XTS is enabled the suite name becomes part of the banner, so a captured
   serial log can always be matched to the exact image that produced it. With one
   image per suite this is the only way to tell the logs apart, and the
   compatibility review asks for exactly that. */
#ifdef BOARD_XTS_SUITE
#define BOARD_XTS_SUITE_TAG   "-" BOARD_XTS_SUITE
#else
#define BOARD_XTS_SUITE_TAG   ""
#endif

#define BOOT_VERSION_STR   "[N32H7] BUILD=v58-logs" BOARD_XTS_SUITE_TAG "\r\n"

/* Send a NUL-terminated string straight to the UART, one byte at a time.
   Deliberately avoids printf/vsnprintf so the boot path carries no libc
   formatting dependency. Exported for the board_led.c step markers. */
void BootPutsRaw(const char *s)
{
    while (*s != '\0') {
        UartPutc((int)(unsigned char)*s);
        s++;
    }
}

static void BootPuts(const char *s)
{
    BootPutsRaw(s);
}

/* 32-bit value as 8 hex digits, no libc dependency. */
static void BootPutHex(unsigned int v)
{
    static const char hex[] = "0123456789abcdef";
    char buf[8];
    int i;

    for (i = 7; i >= 0; i--) {
        buf[i] = hex[v & 0xF];
        v >>= 4;
    }
    for (i = 0; i < 8; i++) {
        UartPutc((int)(unsigned char)buf[i]);
    }
}

/* "<tag>0x1234abcd\r\n" */
void BootPutHex32(const char *tag, unsigned int v)
{
    BootPutsRaw(tag);
    BootPutHex(v);
    BootPutsRaw("\r\n");
}

/* Unsigned value in decimal, no libc dependency. */
static void BootPutDec(unsigned int v)
{
    char buf[12];
    int n = 0;

    if (v == 0U) {
        UartPutc('0');
        return;
    }
    while (v > 0U && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (n > 0) {
        UartPutc((int)(unsigned char)buf[--n]);
    }
}

/* One block per initcall layer: name, entry count, and the addresses of the
   first few entries - i.e. the functions OHOS_SystemInit() is about to call. */
static void BootReportInitLayer(const char *layer, BootInitCall *start, BootInitCall *end)
{
    unsigned int count = (unsigned int)(end - start);
    unsigned int i;

    BootPutsRaw("                  ");
    BootPutsRaw(layer);
    BootPutsRaw(" : ");
    BootPutDec(count);
    BootPutsRaw(" entry\r\n");

    for (i = 0U; i < count && i < 4U; i++) {
        BootPutsRaw("                     entry[");
        BootPutDec(i);
        BootPutsRaw("] = 0x");
        BootPutHex((unsigned int)(unsigned long)start[i]);
        BootPutsRaw("\r\n");
    }
}

/* ---- Cortex-M7 core registers (defined locally: this file must not pull in
        the full device header, only the core CMSIS registers). ---- */
#define CORE_SCB_VTOR      (*(volatile unsigned int *)0xE000ED08UL)
#define CORE_SHCSR         (*(volatile unsigned int *)0xE000ED24UL)
#define CORE_CFSR          (*(volatile unsigned int *)0xE000ED28UL)
#define CORE_HFSR          (*(volatile unsigned int *)0xE000ED2CUL)
#define CORE_BFAR          (*(volatile unsigned int *)0xE000ED38UL)

/* Read CONTROL and MSP via inline asm (no device header needed). */
static unsigned int ReadControl(void)
{
    unsigned int v;
    __asm volatile("mrs %0, control" : "=r"(v));
    return v;
}

static unsigned int ReadMsp(void)
{
    unsigned int v;
    __asm volatile("mrs %0, msp" : "=r"(v));
    return v;
}

/*
 * Optional hardware diagnostics. Set to 1 to dump the core-register snapshots
 * (before/after LOS_KernelInit) and to probe the GPIOB registers one access at
 * a time. Handy whenever the console dies early, because it localises the exact
 * access that faults. Costs boot time and one stack slot, so a released image
 * defaults to 0. The banner and the BUILD string are always kept.
 */
#define BOARD_DEBUG_PROBE 0

/*
 * Pre-kernel I2C bring-up probes ([3c] one byte, [3d] the SDK example's write
 * sequence). They put real traffic on the bus before the kernel starts, which is
 * what made the missing address shift visible at all, so keep them available -
 * but they cost boot time and write to the EEPROM, so a released image leaves
 * them off. Set to 1 when touching I2C pins, clock or timing.
 */
#define BOARD_I2C_PREKERNEL_PROBE 0

LITE_OS_SEC_TEXT_INIT int main(void)
{
    UINT32 ret;

    /* 1) SDK Mode0 clock (M7 core 600 MHz, AHB 300 MHz, APB 150 MHz) and the
          CM4 release, both before any peripheral is touched. */
    BoardClockInit();

    /* 2) LED2 (PB3) on as early as possible: shows main() started even when the
          console misbehaves. Runs before the kernel, so no scheduler needed. */
    BoardLedEarlyOn();

    /* 3) Console first, so any later failure is visible. */
    UartInit();

    /* 4) I2C1 pin mux (PB6 = SCL, PB7 = SDA), done HERE and not in the HDF
          driver: the SDK's GPIO helper must be called before the kernel starts,
          see the note in n32_i2c_hw.h. */
    {
        extern int32_t N32I2cHwPinInit(uint8_t port);

        if (N32I2cHwPinInit(1) != 0) {
            BootPuts("[N32H7] I2C1 pin init FAILED\r\n");
        } else {
            BootPuts("[N32H7] [3b] I2C1 pin mux : PB6/PB7 ready\r\n");
        }

    /* 5) PWM pins (GTIMA1_CH1 = PA0, CH2 = PA1), same pre-kernel constraint as
          the I2C pins above: the SDK's GPIO helper must not run from a driver. */
    {
        extern int32_t N32PwmHwPinInit(void);

        if (N32PwmHwPinInit() != 0) {
            BootPuts("[N32H7] PWM pin init FAILED\r\n");
        } else {
            BootPuts("[N32H7] [3b] PWM pin mux : ");
            BootPuts(N32PwmHwName());
            BootPuts(" ready, PA1 untouched (ETH_REFCLK)\r\n");
        }
    }

#if BOARD_I2C_PREKERNEL_PROBE
        /* Bring-up probes, off in a released image - see the macro.
           [3c] one byte on the bus right now, same configuration as the SDK
           example: tells apart "hardware/clock wrong" from "only the post-kernel
           path is wrong".
           [3d] the SDK I2C_EEPROM example's write sequence verbatim (see
           N32I2cHwDemoWrite). Returns 0 on success or the failing step number:
           -2 bus never idle, -3 no WRAVL for the memory address,
           -4 no WRAVL for a data byte, -5 no STOP. The address is the 7-bit form
           (0x50), matching the example's EEPROM_BLOCK0_ADDRESS 0xA0 - the shim
           performs the single left shift the N32 SADR field requires.
           Both run before the kernel, exactly where the vendor examples do their
           transfers, so a pass proves pins + clock + EEPROM + timing are good and
           isolates the post-kernel/HDF path from the hardware. */
        {
            extern int32_t N32I2cHwSelfTest(uint8_t port);
            int32_t st = N32I2cHwSelfTest(1);

            BootPuts(st == 0  ? "[N32H7] [3c] I2C1 selftest : OK (byte written)\r\n"
                   : st == -1 ? "[N32H7] [3c] I2C1 selftest : bad port\r\n"
                   : st == -2 ? "[N32H7] [3c] I2C1 selftest : BUSY stuck\r\n"
                              : "[N32H7] [3c] I2C1 selftest : WRAVL never came\r\n");
        }

        {
            extern int32_t N32I2cHwDemoWrite(uint8_t port, uint8_t devAddr, uint8_t memAddr,
                                             const uint8_t *pBuf, uint8_t nBytes);
            static uint8_t data[8];
            uint8_t        i;
            int32_t        r;

            for (i = 0U; i < 8U; i++) {
                data[i] = (uint8_t)(0xA0U + i);
            }
            r = N32I2cHwDemoWrite(1U, 0x50U, 0x00U, data, 8U);
            BootPuts("[N32H7] [3d] SDK-example write step = ");
            BootPutDec((unsigned int)r);
            BootPuts("\r\n");
        }
#endif /* BOARD_I2C_PREKERNEL_PROBE */
    }

    BootPuts("\r\n============ OpenHarmony LiteOS-M (mini) ============\r\n");
    BootPuts(BOOT_VERSION_STR);
    BootPuts("[N32H7] [1/7] clock    : M7 600MHz / AHB 300MHz / APB 150MHz, CM4 released\r\n");
    BootPuts("[N32H7] [2/7] early led: PB3 low (LED2 on)\r\n");
    BootPuts("[N32H7] [3/7] console  : USART1 PA9/PA10 @115200 8N1\r\n");

    /* ---- optional: snapshot core state BEFORE the kernel touches anything. */
#if BOARD_DEBUG_PROBE
    BootPutHex32("[N32H7] pre-kernel VTOR = 0x", CORE_SCB_VTOR);
    BootPutHex32("[N32H7] pre-kernel SHCSR= 0x", CORE_SHCSR);
    BootPutHex32("[N32H7] pre-kernel CFSR = 0x", CORE_CFSR);
    BootPutHex32("[N32H7] pre-kernel MSP  = 0x", ReadMsp());
    BootPutHex32("[N32H7] pre-kernel CTRL = 0x", ReadControl());
#endif

    /* 4) LiteOS-M kernel. */
    ret = LOS_KernelInit();
    if (ret != LOS_OK) {
        BootPuts("[N32H7] [4/7] kernel   : LOS_KernelInit FAILED\r\n");
        while (1) {
            __asm volatile("wfi");
        }
    }
    BootPuts("[N32H7] [4/7] kernel   : LOS_KernelInit() OK\r\n");

    /* ---- optional: snapshot core state AFTER kernel init. The diff shows what
       the kernel changed, including fault reporting it may have enabled. ---- */
#if BOARD_DEBUG_PROBE
    BootPutHex32("[N32H7] post-kernel VTOR = 0x", CORE_SCB_VTOR);
    BootPutHex32("[N32H7] post-kernel SHCSR= 0x", CORE_SHCSR);
    BootPutHex32("[N32H7] post-kernel CFSR = 0x", CORE_CFSR);
    BootPutHex32("[N32H7] post-kernel HFSR = 0x", CORE_HFSR);
#endif

    /* Clear pending fault flags so the next fault is definitely ours. */
    CORE_CFSR = CORE_CFSR;
    CORE_HFSR = CORE_HFSR;

    /* ---- optional: raw register probe. Pin configuration is deliberately done
       before the kernel (see the reference pattern), so this only touches the
       GPIOB data register, one access at a time, to localise a bus fault. ---- */
#if BOARD_DEBUG_PROBE
    BootPuts("[N32H7] probe: read GPIOB data register\r\n");
    {
        volatile unsigned int v = *(volatile unsigned int *)(0x58032C00UL + 0x14UL);
        (void)v;
    }
    BootPuts("[N32H7] probe: read ok\r\n");

    BootPuts("[N32H7] probe: write GPIOB data register\r\n");
    *(volatile unsigned int *)(0x58032C00UL + 0x14UL) = 0x8U;
    BootPuts("[N32H7] probe: write ok\r\n");
#endif

    BootPuts("[N32H7] --------------- OpenHarmony startup ---------------\r\n");

    /* 5) HDF: turn the compiled-in .hcs blob into hosts, devices and driver
          instances, then run Bind/Init on every matched driver. */
    BootPuts("[N32H7] [5/7] hdf      : DeviceManagerStart()\r\n");
    ret = (UINT32)DeviceManagerStart();
    BootPutHex32("[N32H7]                  ret = 0x", ret);

    /* 6) OpenHarmony bootstrap. OHOS_SystemInit() runs the six initcall layers
          in the order printed below; the application is one of the run-layer
          entries, which is why it is never called from main(). */
    BootPuts("[N32H7] [6/7] bootstrap: OHOS_SystemInit()\r\n");
    BootReportInitLayer("MODULE_INIT(bsp)     ", __zinitcall_bsp_start, __zinitcall_bsp_end);
    BootReportInitLayer("MODULE_INIT(device)  ", __zinitcall_device_start, __zinitcall_device_end);
    BootReportInitLayer("MODULE_INIT(core)    ", __zinitcall_core_start, __zinitcall_core_end);
    BootReportInitLayer("SYS_INIT(sys_service)", __zinitcall_sys_service_start, __zinitcall_sys_service_end);
    BootReportInitLayer("SYS_INIT(sys_feature)", __zinitcall_sys_feature_start, __zinitcall_sys_feature_end);
    BootReportInitLayer("MODULE_INIT(run)     ", __zinitcall_run_start, __zinitcall_run_end);

    OHOS_SystemInit();
    BootPuts("[N32H7]                  OHOS_SystemInit() done\r\n");

#if BOARD_RUN_XTS
    /*
     * XTS: the suites are run by hctest itself, NOT from here.
     *
     * TestService (test/xts/tools/lite/hctest/src/hctest_service.c) registers with
     * samgr; SAMGR_Bootstrap(), called at the end of OHOS_SystemInit(), makes it
     * post itself a MSG_START_TEST and its MessageHandle() then calls
     * INIT_TEST_CALL(), which walks this very `test` initcall layer. That runs on
     * the service's own task, i.e. after LOS_Start().
     *
     * Walking the layer here as well would execute every suite twice, so this
     * block only reports what is registered.
     */
    {
        unsigned int xtsCount = (unsigned int)(__zinitcall_test_end - __zinitcall_test_start);

#ifdef BOARD_XTS_SUITE
        BootPuts("[N32H7] [xts]      : suite=" BOARD_XTS_SUITE ", ");
#else
        BootPuts("[N32H7] [xts]      : ");
#endif
        BootPutDec(xtsCount);
        BootPuts(" suite entry(s) registered (hctest runs them after LOS_Start)\r\n");
    }
#endif

    BootPuts("[N32H7] [7/7] scheduler: LOS_Start()\r\n");
    BootPuts("[N32H7] ==================== running ====================\r\n");

    LOS_Start();

    while (1) {
        __asm volatile("wfi");
    }
}

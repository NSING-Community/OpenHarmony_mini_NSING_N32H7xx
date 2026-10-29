/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file uart_n32h7xx.c
 * @brief HDF platform UART driver for N32H7xx (LiteOS-M / Cortex-M7).
 *
 * Covers the console UART only (USART1, PA9/PA10, 115200 8N1).
 *
 * The pin muxing, GPIO clock and the USART1 clock tree are already brought up
 * by the board layer (board/src/uart_console.c: BoardClockInit + UartInit)
 * BEFORE the kernel starts, so this driver must not re-initialise the
 * peripheral -- it only takes over the data path and exposes it through the
 * HDF UART interface.
 *
 * Application usage:
 *     DevHandle h = UartOpen(0);      // 0 -> USART1 (service HDF_PLATFORM_UART_0)
 *     UartWrite(h, (uint8_t *)"hi\r\n", 4);
 *
 * Data path note: wait on TXDE (data register EMPTY), NOT TXC (transmission
 * complete). Waiting on TXC deadlocks on bursts -- see uart_console.c.
 *
 * IMPORTANT - why the registers are declared locally:
 *   This translation unit is compiled inside the HDF build, whose include
 *   chain already pulls in target_config.h -> soc.h, and soc.h declares
 *   "enum IRQn". The CMSIS device header n32h7xx.h declares the SAME enum, so
 *   including it here is a redeclaration error. Only the numeric register
 *   layout is needed, so it is mirrored below.
 *
 * Register map (USART_Module):
 *   0x0C STS  status   (TXDE = bit7 = 0x080, RXDNE = bit9 = 0x200)
 *   0x10 DAT  data
 * Base: APB1PERIPH_BASE(0x40000000) + 0xC400 = 0x4000C400
 */

#include "uart_core.h"
#include "hdf_core_log.h"

#define HDF_LOG_TAG uart_n32h7xx

#define N32_UART_CONSOLE_ID     1U
#define N32_UART_DEFAULT_BAUD   115200U

#define N32_UART1_BASE          0x4000C400UL
#define N32_UART_STS_OFFSET     0x0CUL
#define N32_UART_DAT_OFFSET     0x10UL

#define N32_UART_STS_TXDE       0x00000080UL    /* transmit data register empty */
#define N32_UART_STS_RXDNE      0x00000200UL    /* read data register not empty */

#define N32_REG32(addr)         (*(volatile uint32_t *)(addr))

typedef struct {
    uint32_t uartId;
    uint32_t baudRate;
} UartDevice;

static UartDevice g_n32UartDevice = {
    .uartId = N32_UART_CONSOLE_ID,
    .baudRate = N32_UART_DEFAULT_BAUD,
};

/* ------------------------------------------------------------------ */
/* driver entry (self-registered into the .hdf.driver section)         */
/* ------------------------------------------------------------------ */
static int32_t UartDriverBind(struct HdfDeviceObject *device);
static int32_t UartDriverInit(struct HdfDeviceObject *device);
static void UartDriverRelease(struct HdfDeviceObject *device);

struct HdfDriverEntry g_UartDriverEntry = {
    .moduleVersion = 1,
    .moduleName = "NSING_UART_MODULE_HDF",   /* must match device_info.hcs */
    .Bind = UartDriverBind,
    .Init = UartDriverInit,
    .Release = UartDriverRelease,
};
HDF_INIT(g_UartDriverEntry);

/* ------------------------------------------------------------------ */
/* UartHostMethod hooks                                                */
/* ------------------------------------------------------------------ */
static int32_t UartHostDevInit(struct UartHost *host);
static int32_t UartHostDevDeinit(struct UartHost *host);
static int32_t UartHostDevWrite(struct UartHost *host, uint8_t *data, uint32_t size);
static int32_t UartHostDevRead(struct UartHost *host, uint8_t *data, uint32_t size);
static int32_t UartHostDevSetBaud(struct UartHost *host, uint32_t baudRate);
static int32_t UartHostDevGetBaud(struct UartHost *host, uint32_t *baudRate);

static struct UartHostMethod g_n32UartHostMethod = {
    .Init = UartHostDevInit,
    .Deinit = UartHostDevDeinit,
    .Read = UartHostDevRead,
    .Write = UartHostDevWrite,
    .GetBaud = UartHostDevGetBaud,
    .SetBaud = UartHostDevSetBaud,
    .GetAttribute = NULL,
    .SetAttribute = NULL,
    .SetTransMode = NULL,
    .pollEvent = NULL,
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
static uint32_t N32UartBase(uint32_t id)
{
    /* Only the console UART is handled by this driver. */
    return (id == N32_UART_CONSOLE_ID) ? N32_UART1_BASE : 0UL;
}

/* ------------------------------------------------------------------ */
/* method implementations                                              */
/* ------------------------------------------------------------------ */
static int32_t UartHostDevInit(struct UartHost *host)
{
    UartDevice *dev;

    if (host == NULL || host->priv == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }
    dev = (UartDevice *)host->priv;
    HDF_LOGI("%s: uart%u already initialised by the board layer", __func__, dev->uartId);
    return HDF_SUCCESS;
}

static int32_t UartHostDevDeinit(struct UartHost *host)
{
    (void)host;
    return HDF_SUCCESS;
}

static int32_t UartHostDevWrite(struct UartHost *host, uint8_t *data, uint32_t size)
{
    UartDevice *dev;
    uint32_t base;
    uint32_t i;

    if (host == NULL || data == NULL || size == 0U || host->priv == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }
    dev = (UartDevice *)host->priv;
    base = N32UartBase(dev->uartId);
    if (base == 0UL) {
        return HDF_ERR_INVALID_PARAM;
    }

    for (i = 0U; i < size; i++) {
        while ((N32_REG32(base + N32_UART_STS_OFFSET) & N32_UART_STS_TXDE) == 0U) {
            /* wait until the transmit data register is empty */
        }
        N32_REG32(base + N32_UART_DAT_OFFSET) = data[i];
    }
    return (int32_t)size;
}

static int32_t UartHostDevRead(struct UartHost *host, uint8_t *data, uint32_t size)
{
    UartDevice *dev;
    uint32_t base;
    uint32_t i;

    if (host == NULL || data == NULL || size == 0U || host->priv == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }
    dev = (UartDevice *)host->priv;
    base = N32UartBase(dev->uartId);
    if (base == 0UL) {
        return HDF_ERR_INVALID_PARAM;
    }

    for (i = 0U; i < size; i++) {
        if ((N32_REG32(base + N32_UART_STS_OFFSET) & N32_UART_STS_RXDNE) == 0U) {
            break;                      /* non-blocking: return what is available */
        }
        data[i] = (uint8_t)N32_REG32(base + N32_UART_DAT_OFFSET);
    }
    return (int32_t)i;
}

static int32_t UartHostDevSetBaud(struct UartHost *host, uint32_t baudRate)
{
    UartDevice *dev;

    if (host == NULL || host->priv == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }
    dev = (UartDevice *)host->priv;

    /* The USART registers are programmed once at board init (115200). Nothing
     * here touches them, so accepting another rate would be a false success. */
    if (baudRate != dev->baudRate) {
        HDF_LOGE("%s: uart%u rate %u unsupported (fixed at %u)",
                 __func__, dev->uartId, baudRate, dev->baudRate);
        return HDF_ERR_NOT_SUPPORT;
    }
    return HDF_SUCCESS;
}

static int32_t UartHostDevGetBaud(struct UartHost *host, uint32_t *baudRate)
{
    UartDevice *dev;

    if (host == NULL || baudRate == NULL || host->priv == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }
    dev = (UartDevice *)host->priv;
    *baudRate = dev->baudRate;
    return HDF_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* driver lifecycle                                                    */
/* ------------------------------------------------------------------ */
static int32_t UartDriverBind(struct HdfDeviceObject *device)
{
    if (device == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }
    return (UartHostCreate(device) == NULL) ? HDF_FAILURE : HDF_SUCCESS;
}

static int32_t UartDriverInit(struct HdfDeviceObject *device)
{
    struct UartHost *host = NULL;

    if (device == NULL) {
        return HDF_ERR_INVALID_OBJECT;
    }
    host = UartHostFromDevice(device);
    if (host == NULL) {
        HDF_LOGE("%s: host is NULL", __func__);
        return HDF_ERR_INVALID_OBJECT;
    }

    host->priv = &g_n32UartDevice;
    host->method = &g_n32UartHostMethod;

    HDF_LOGI("%s: N32H7xx UART%u registered (HDF_PLATFORM_UART_0)", __func__, N32_UART_CONSOLE_ID);
    return HDF_SUCCESS;
}

static void UartDriverRelease(struct HdfDeviceObject *device)
{
    struct UartHost *host = NULL;

    if (device == NULL) {
        return;
    }
    host = UartHostFromDevice(device);
    if (host != NULL) {
        UartHostDestroy(host);
    }
}

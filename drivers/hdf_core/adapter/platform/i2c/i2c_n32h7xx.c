/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file i2c_n32h7xx.c
 * @brief HDF platform I2C driver for N32H7xx (LiteOS-M / Cortex-M7).
 *
 * Serves I2C1 (PB6 = SCL, PB7 = SDA - the pins the AT24 and the SDK I2C_EEPROM
 * example use) and I2C4 (PD12/PD13, shared with GTIMA3 CH1/CH2 so the I2C and
 * PWM demos swap without re-wiring).
 *
 * struct I2cMsg carries the 7-bit slave address, as it does for every other HDF
 * I2C driver (i2c_stm32f4xx.c shifts left by one before touching the
 * peripheral). The N32 controller also wants the 8-bit bus byte, so the shift
 * lives in the SoC shim - this file passes msg->addr straight through.
 *
 * This file deliberately includes NO vendor device header. The HDF header chain
 * reaches soc.h, which declares `enum IRQn`; n32h7xx.h declares the same enum and
 * neither is guarded, so pulling in both fails with "redeclaration of 'enum
 * IRQn'". All register-level work sits behind n32_i2c_hw.h instead.
 *
 * The device_info.hcs node uses policy = 0: the I2C interface locates the
 * controller by bus id rather than opening a device by service name, so the node
 * must not publish a service. Same as every vendor in the tree (pwm_config.hcs
 * uses serviceName = "").
 */

#include "device_resource_if.h"
#include "hdf_device_desc.h"
#include "hdf_core_log.h"
#include "i2c_core.h"
#include "osal_mutex.h"

#include "n32_i2c_hw.h"

#define HDF_LOG_TAG i2c_n32h7xx

/*
 * I2C_DIAG writes straight to the USART1 registers, bypassing hilog. hilog is
 * initialised after DeviceManagerStart(), so HDF_LOGI() from Init() would be
 * lost exactly when we most need it. Keep 1 while bringing the driver up; set 0
 * for a released image (the console must already be up, which it is by then).
 */
#define I2C_DIAG 1

#if I2C_DIAG
#define DIAG_USART_BASE     0x4000C400UL
#define DIAG_STS_OFFSET     0x0CUL
#define DIAG_DAT_OFFSET     0x10UL
#define DIAG_STS_TXDE       0x00000080UL
#define DIAG_REG32(addr)    (*(volatile uint32_t *)(addr))

static void DiagPuts(const char *s);

static void DiagPutHex(uint32_t v)
{
    static const char dig[] = "0123456789abcdef";
    int32_t           i;

    DiagPuts("0x");
    for (i = 7; i >= 0; i--) {
        char c[2];
        c[0] = dig[(v >> (i * 4)) & 0xFU];
        c[1] = '\0';
        DiagPuts(c);
    }
}


static void DiagPutDec(int32_t v)
{
    char    buf[12];
    int32_t i = 0;

    if (v < 0) {
        DiagPuts("-");
        v = -v;
    }
    if (v == 0) {
        DiagPuts("0");
        return;
    }
    while (v > 0 && i < 11) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        char c[2];
        c[0] = buf[--i];
        c[1] = '\0';
        DiagPuts(c);
    }
}

static void DiagPuts(const char *s)
{
    while (*s != '\0') {
        while ((DIAG_REG32(DIAG_USART_BASE + DIAG_STS_OFFSET) & DIAG_STS_TXDE) == 0U) {
        }
        DIAG_REG32(DIAG_USART_BASE + DIAG_DAT_OFFSET) = (uint8_t)(*s++);
    }
}
#else
/* All three must be defined: the driver still calls DiagPutDec / DiagPutHex
 * when diagnostics are compiled out. Defining only DiagPuts broke the build. */
#define DiagPuts(s)    do { } while (0)
#define DiagPutDec(v)  do { } while (0)
#define DiagPutHex(v)  do { } while (0)
#endif

struct N32I2cRes {
    uint8_t  port;
    uint32_t speed;
    struct OsalMutex mutex;
};

static struct I2cCntlr  g_n32I2cCntlr;
static struct N32I2cRes g_n32I2cRes;

static int32_t N32I2cBind(struct HdfDeviceObject *device);
static int32_t N32I2cInit(struct HdfDeviceObject *device);
static void    N32I2cRelease(struct HdfDeviceObject *device);
static int32_t N32I2cTransfer(struct I2cCntlr *cntlr, struct I2cMsg *msgs, int16_t count);

struct HdfDriverEntry g_N32I2cDriverEntry = {
    .moduleVersion = 1,
    .moduleName    = "NSING_I2C_MODULE_HDF",
    .Bind          = N32I2cBind,
    .Init          = N32I2cInit,
    .Release       = N32I2cRelease,
};
HDF_INIT(g_N32I2cDriverEntry);

static struct I2cMethod g_n32I2cMethod = {
    .transfer = N32I2cTransfer,
};

/**
 * @brief One I2cMsg, polled.
 *
 * An EEPROM-style sequence arrives as two messages ("write the register
 * address" then "read N bytes"), so the framework owns the repeated-start
 * handling and this only has to move one buffer.
 */
static int32_t N32I2cOneMsg(const struct N32I2cRes *res, struct I2cMsg *msg)
{
    /* A zero-length write is the standard bus-scan probe (i2c-tools does the
       same): send the address only and read the ACK bit. A NAK here is a valid
       answer, not a driver error - the caller reads it from the return value:
       success = slave present, failure = nobody at that address. */
    if (msg->len == 0 && msg->flags != I2C_FLAG_READ) {
        return (N32I2cHwProbe(res->port, msg->addr) == N32_I2C_OK) ?
               HDF_SUCCESS : HDF_FAILURE;
    }

    if (msg->len == 0 || msg->buf == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }

    if (msg->flags == I2C_FLAG_READ) {
        int32_t r = N32I2cHwRead(res->port, msg->addr, msg->buf, msg->len);

        if (r != N32_I2C_OK) {
            DiagPuts("[I2C] read err=");
            DiagPutDec(r);
            DiagPuts("\r\n");
            return HDF_FAILURE;
        }
    } else {
        int32_t r = N32I2cHwWrite(res->port, msg->addr, msg->buf, msg->len);

        if (r != N32_I2C_OK) {
            uint32_t c1 = 0U, c2 = 0U, st = 0U;

            N32I2cHwFaultSnapshot(&c1, &c2, &st);
            DiagPuts("[I2C] write err=");
            DiagPutDec(r);
            DiagPuts(" CTRL1=");
            DiagPutHex(c1);
            DiagPuts(" CTRL2=");
            DiagPutHex(c2);
            DiagPuts(" STSINT=");
            DiagPutHex(st);
            DiagPuts("\r\n");
            return HDF_FAILURE;
        }
    }
    return HDF_SUCCESS;
}

static int32_t N32I2cTransfer(struct I2cCntlr *cntlr, struct I2cMsg *msgs, int16_t count)
{
    struct N32I2cRes *res = NULL;
    int16_t           i;

    if (cntlr == NULL || cntlr->priv == NULL || msgs == NULL || count <= 0) {
        return HDF_ERR_INVALID_PARAM;
    }
    res = (struct N32I2cRes *)cntlr->priv;

    if (OsalMutexLock(&res->mutex) != HDF_SUCCESS) {
        return HDF_ERR_TIMEOUT;
    }

    for (i = 0; i < count; i++) {
        if (N32I2cOneMsg(res, &msgs[i]) != HDF_SUCCESS) {
            (void)OsalMutexUnlock(&res->mutex);
            return HDF_FAILURE;
        }
    }

    (void)OsalMutexUnlock(&res->mutex);
    return count;
}

/**
 * @brief Read the hcs node, falling back to defaults.
 *
 * The node arrives from HdfDeviceNodeConstruct(), which looks it up by
 * `deviceMatchAttr` in the compiled hcs tree - so the values live in
 * vendor/Nsing/n32h7xx_eval/hdf_config/i2c/i2c_config.hcs (match_attr =
 * "n32h7xx_i2c"), not inside the device_info.hcs deviceNode. Get that pair
 * wrong and property is NULL, which is why the fallback below exists.
 *
 * Deliberately tolerant: this board only brings out I2C1 and I2C4, so a missing
 * or unreadable node is not a reason to fail Init() - defaulting to I2C1 at
 * 100 kHz keeps the controller usable (and that default happens to be the
 * board's wiring, so it must stay in step with the hcs).
 */
static int32_t N32I2cGetResource(struct N32I2cRes *res, const struct DeviceResourceNode *node)
{
    struct DeviceResourceIface *dri = NULL;

    res->port  = N32_I2C_PORT_1;
    res->speed = 100000;

    if (node == NULL) {
        DiagPuts("[I2C] no hcs node -> default port=1\r\n");
        return HDF_SUCCESS;
    }

    dri = DeviceResourceGetIfaceInstance(HDF_CONFIG_SOURCE);
    if (dri == NULL || dri->GetUint8 == NULL || dri->GetUint32 == NULL) {
        DiagPuts("[I2C] no resource iface -> default port=1\r\n");
        return HDF_SUCCESS;
    }

    if (dri->GetUint8(node, "port", &res->port, 0) != HDF_SUCCESS) {
        res->port = N32_I2C_PORT_1;
        DiagPuts("[I2C] no 'port' prop -> default port=1\r\n");
    }
    (void)dri->GetUint32(node, "speed", &res->speed, 100000);

    if (res->port != N32_I2C_PORT_1 && res->port != N32_I2C_PORT_4) {
        DiagPuts("[I2C] port not brought out -> forced to 1\r\n");
        res->port = N32_I2C_PORT_1;
    }
    return HDF_SUCCESS;
}

/**
 * @brief Bind hook.
 *
 * policy = 0 means no device service is published, so there is nothing to
 * create here beyond validating the argument.
 */
static int32_t N32I2cBind(struct HdfDeviceObject *device)
{
    if (device == NULL) {
        HDF_LOGE("%s: device is NULL", __func__);
        return HDF_ERR_INVALID_PARAM;
    }
    return HDF_SUCCESS;
}

static int32_t N32I2cInit(struct HdfDeviceObject *device)
{
    int32_t ret;

    DiagPuts("\r\n[I2C] driver Init() entered\r\n");
    if (device == NULL) {
        DiagPuts("[I2C] ERR: device is NULL\r\n");
        return HDF_ERR_INVALID_PARAM;
    }

    ret = N32I2cGetResource(&g_n32I2cRes, device->property);
    if (ret != HDF_SUCCESS) {
        DiagPuts("[I2C] ERR: GetResource\r\n");
        return ret;
    }
    if (OsalMutexInit(&g_n32I2cRes.mutex) != HDF_SUCCESS) {
        DiagPuts("[I2C] ERR: MutexInit\r\n");
        return HDF_FAILURE;
    }

    DiagPuts((N32I2cHwPll3Status() != 0)
                 ? "[I2C] clock: PLL3C 48MHz, timing 0x50012526\r\n"
                 : "[I2C] clock: PLL3 failed, HSI 64MHz direct, timing 0xF0001313\r\n");
    DiagPuts("[I2C] post-init BUSY=");
    DiagPutDec(N32I2cHwBusy(g_n32I2cRes.port));
    DiagPuts("\r\n");

    if (N32I2cHwInit(g_n32I2cRes.port, g_n32I2cRes.speed) != N32_I2C_OK) {
        DiagPuts("[I2C] ERR: HwInit (clock/pins/peripheral)\r\n");
        (void)OsalMutexDestroy(&g_n32I2cRes.mutex);
        return HDF_FAILURE;
    }

    g_n32I2cCntlr.ops   = &g_n32I2cMethod;
    g_n32I2cCntlr.busId = (int16_t)g_n32I2cRes.port;
    g_n32I2cCntlr.priv  = &g_n32I2cRes;

    ret = I2cCntlrAdd(&g_n32I2cCntlr);
    if (ret != HDF_SUCCESS) {
        DiagPuts("[I2C] ERR: I2cCntlrAdd\r\n");
        (void)OsalMutexDestroy(&g_n32I2cRes.mutex);
        return ret;
    }

    HDF_LOGI("%s: I2C%u registered (HDF_PLATFORM_I2C_%u)", __func__,
             g_n32I2cRes.port, g_n32I2cRes.port);
    DiagPuts("[I2C] cntlr added, driver registered OK\r\n");
    return HDF_SUCCESS;
}

static void N32I2cRelease(struct HdfDeviceObject *device)
{
    (void)device;

    I2cCntlrRemove(&g_n32I2cCntlr);
    g_n32I2cCntlr.ops  = NULL;
    g_n32I2cCntlr.priv = NULL;
    (void)OsalMutexDestroy(&g_n32I2cRes.mutex);
}

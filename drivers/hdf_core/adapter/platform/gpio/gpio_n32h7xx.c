/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file gpio_n32h7xx.c
 * @brief HDF platform GPIO driver for N32H7xx (LiteOS-M / Cortex-M7).
 *
 * GPIO_DIAG drives progress reports straight to the USART1 registers so they
 * survive the fact that hilog is initialised later than DeviceManagerStart().
 * Useful whenever the console dies before hilog starts; 0 in a released image.
 */

#include "gpio_core.h"
#include "hdf_core_log.h"
#include "osal_irq.h"

#define HDF_LOG_TAG gpio_n32h7xx

#define GPIO_DIAG   0

#define N32_GPIO_PIN_MAX    16U
#define N32_GPIO_GROUP_MAX  9U
#define N32_GPIO_TOTAL      (N32_GPIO_GROUP_MAX * N32_GPIO_PIN_MAX)

#define N32_GPIO_BASE(group)    (0x58032800UL + (uint32_t)(group) * 0x400UL)
#define N32_GPIO_PMODE_OFFSET   0x00UL
#define N32_GPIO_PID_OFFSET     0x10UL
#define N32_GPIO_POD_OFFSET     0x14UL
#define N32_GPIO_PMODE_INPUT    0x0UL
#define N32_GPIO_PMODE_OUTPUT   0x1UL

#define N32_REG32(addr)         (*(volatile uint32_t *)(addr))

#if GPIO_DIAG
/* USART1 (console) - same peripheral the board layer already brought up. */
#define DIAG_USART_BASE         0x4000C400UL
#define DIAG_STS_OFFSET         0x0CUL
#define DIAG_DAT_OFFSET         0x10UL
#define DIAG_STS_TXDE           0x00000080UL

static void DiagPutc(char c)
{
    while ((N32_REG32(DIAG_USART_BASE + DIAG_STS_OFFSET) & DIAG_STS_TXDE) == 0U) {
    }
    N32_REG32(DIAG_USART_BASE + DIAG_DAT_OFFSET) = (uint8_t)c;
}

static void DiagPuts(const char *s)
{
    while (*s != '\0') {
        DiagPutc(*s++);
    }
}

static void DiagHex(uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    int32_t i;
    DiagPuts("0x");
    for (i = 7; i >= 0; i--) {
        DiagPutc(d[(v >> (i * 4)) & 0xFU]);
    }
}
#endif

static struct GpioCntlr g_n32GpioCntlr;

static int32_t GpioDriverInit(struct HdfDeviceObject *device);
static void GpioDriverRelease(struct HdfDeviceObject *device);
static int32_t GpioDriverBind(struct HdfDeviceObject *device);

struct HdfDriverEntry g_GpioDriverEntry = {
    .moduleVersion = 1,
    .moduleName = "NSING_GPIO_MODULE_HDF",
    .Bind = GpioDriverBind,
    .Init = GpioDriverInit,
    .Release = GpioDriverRelease,
};
HDF_INIT(g_GpioDriverEntry);

static int32_t GpioDevWrite(struct GpioCntlr *cntlr, uint16_t local, uint16_t val);
static int32_t GpioDevRead(struct GpioCntlr *cntlr, uint16_t local, uint16_t *val);
static int32_t GpioDevSetDir(struct GpioCntlr *cntlr, uint16_t local, uint16_t dir);
static int32_t GpioDevGetDir(struct GpioCntlr *cntlr, uint16_t local, uint16_t *dir);

static struct GpioMethod g_n32GpioMethod = {
    .request = NULL,
    .release = NULL,
    .write = GpioDevWrite,
    .read = GpioDevRead,
    .setDir = GpioDevSetDir,
    .getDir = GpioDevGetDir,
    .toIrq = NULL,
    .setIrq = NULL,
    .unsetIrq = NULL,
    .enableIrq = NULL,
    .disableIrq = NULL,
};

static int32_t N32GpioSplit(uint16_t local, uint32_t *group, uint32_t *pin)
{
    if (local >= N32_GPIO_TOTAL) {
        HDF_LOGE("%s: pin %u out of range", __func__, local);
        return HDF_ERR_INVALID_PARAM;
    }
    *group = local / N32_GPIO_PIN_MAX;
    *pin = local % N32_GPIO_PIN_MAX;
    return HDF_SUCCESS;
}

static int32_t GpioDevWrite(struct GpioCntlr *cntlr, uint16_t local, uint16_t val)
{
    uint32_t group;
    uint32_t pin;
    uint32_t addr;

    (void)cntlr;
    if (N32GpioSplit(local, &group, &pin) != HDF_SUCCESS) {
        return HDF_ERR_INVALID_PARAM;
    }
    addr = N32_GPIO_BASE(group) + N32_GPIO_POD_OFFSET;
    if (val != 0U) {
        N32_REG32(addr) |= (1UL << pin);
    } else {
        N32_REG32(addr) &= ~(1UL << pin);
    }
    return HDF_SUCCESS;
}

static int32_t GpioDevRead(struct GpioCntlr *cntlr, uint16_t local, uint16_t *val)
{
    uint32_t group;
    uint32_t pin;
    uint32_t addr;

    (void)cntlr;
    if (val == NULL || N32GpioSplit(local, &group, &pin) != HDF_SUCCESS) {
        return HDF_ERR_INVALID_PARAM;
    }
    addr = N32_GPIO_BASE(group) + N32_GPIO_PID_OFFSET;
    *val = (uint16_t)((N32_REG32(addr) >> pin) & 0x1U);
    return HDF_SUCCESS;
}

static int32_t GpioDevSetDir(struct GpioCntlr *cntlr, uint16_t local, uint16_t dir)
{
    uint32_t group;
    uint32_t pin;
    uint32_t pmode;
    uint32_t addr;

    (void)cntlr;
    if (N32GpioSplit(local, &group, &pin) != HDF_SUCCESS) {
        return HDF_ERR_INVALID_PARAM;
    }
    addr = N32_GPIO_BASE(group) + N32_GPIO_PMODE_OFFSET;
    pmode = (dir == GPIO_DIR_OUT) ? N32_GPIO_PMODE_OUTPUT : N32_GPIO_PMODE_INPUT;
    N32_REG32(addr) &= ~(0x3UL << (pin * 2U));
    N32_REG32(addr) |= (pmode << (pin * 2U));
    return HDF_SUCCESS;
}

static int32_t GpioDevGetDir(struct GpioCntlr *cntlr, uint16_t local, uint16_t *dir)
{
    uint32_t group;
    uint32_t pin;
    uint32_t pmode;
    uint32_t addr;

    (void)cntlr;
    if (dir == NULL || N32GpioSplit(local, &group, &pin) != HDF_SUCCESS) {
        return HDF_ERR_INVALID_PARAM;
    }
    addr = N32_GPIO_BASE(group) + N32_GPIO_PMODE_OFFSET;
    pmode = (N32_REG32(addr) >> (pin * 2U)) & 0x3U;
    *dir = (pmode == N32_GPIO_PMODE_OUTPUT) ? GPIO_DIR_OUT : GPIO_DIR_IN;
    return HDF_SUCCESS;
}

/**
 * @brief Service dispatcher for this platform device.
 *
 * WHY THIS EXISTS:
 *   device_info.hcs declares this node with policy = 2 (SERVICE_POLICY_CAPACITY)
 *   and serviceName = "HDF_PLATFORM_GPIO_0", so HDF runs
 *       DeviceDriverBind()          -> driverEntry->Bind()      [we are here]
 *       driverEntry->Init()
 *       HdfDeviceNodePublishService()
 *                            -> HdfDeviceNodePublishPublicService()
 *   and that last function rejects the node when
 *       devNode->deviceObject.service == NULL
 *   returning HDF_FAILURE.  HdfDeviceLaunchNode() then propagates the error and
 *   the caller frees the node: HdfDeviceNodeDestruct() (devStatus is already
 *   DEVNODE_LAUNCHED) -> HdfDeviceUnlaunchNode() -> driverEntry->Release(),
 *   which unregisters the controller that Init() had just added -- silently,
 *   because hilog is not up yet during DeviceManagerStart().
 *
 *   deviceObject.service comes from device->service (see PlatformDeviceBind()),
 *   and device->service is allocated by PlatformDeviceCreateService().  The UART
 *   driver gets this for free inside UartHostCreate(); the GPIO driver must ask
 *   for it explicitly.
 */
struct HdfDeviceIoClient;
struct HdfSBuf;

static int32_t GpioServiceDispatch(struct HdfDeviceIoClient *client, int cmd,
    struct HdfSBuf *data, struct HdfSBuf *reply)
{
    (void)client;
    (void)cmd;
    (void)data;
    (void)reply;
    return HDF_SUCCESS;
}

/**
 * @brief Driver Bind hook.
 *
 * Runs before Init()/publish, so it is the right place to create the platform
 * device service and attach the platform device to the HDF device object.
 */
static int32_t GpioDriverBind(struct HdfDeviceObject *device)
{
    int32_t ret;

#if GPIO_DIAG
    DiagPuts("\r\n[GPIO] bind enter\r\n");
#endif
    if (device == NULL) {
        HDF_LOGE("%s: device is NULL", __func__);
        return HDF_FAILURE;
    }

    ret = PlatformDeviceCreateService(&g_n32GpioCntlr.device, GpioServiceDispatch);
#if GPIO_DIAG
    DiagPuts("[GPIO] createService ret=");
    DiagHex((uint32_t)ret);
    DiagPuts("\r\n");
#endif
    if (ret != HDF_SUCCESS) {
        return ret;
    }

    ret = PlatformDeviceBind(&g_n32GpioCntlr.device, device);
#if GPIO_DIAG
    DiagPuts("[GPIO] bind ret=");
    DiagHex((uint32_t)ret);
    DiagPuts(" service=");
    DiagHex((uint32_t)(uintptr_t)device->service);
    DiagPuts("\r\n");
#endif
    if (ret != HDF_SUCCESS) {
        (void)PlatformDeviceDestroyService(&g_n32GpioCntlr.device);
        return ret;
    }

    return HDF_SUCCESS;
}

static int32_t GpioDriverInit(struct HdfDeviceObject *device)
{
    int32_t ret;

#if GPIO_DIAG
    DiagPuts("[GPIO] init enter\r\n");
#endif
    if (device == NULL) {
        return HDF_ERR_INVALID_PARAM;
    }

    g_n32GpioCntlr.ops = &g_n32GpioMethod;
    g_n32GpioCntlr.start = 0;
    g_n32GpioCntlr.count = N32_GPIO_TOTAL;

    ret = GpioCntlrAdd(&g_n32GpioCntlr);
#if GPIO_DIAG
    DiagPuts("[GPIO] cntlrAdd ret=");
    DiagHex((uint32_t)ret);
    DiagPuts("\r\n");
#endif
    if (ret != HDF_SUCCESS) {
        return ret;
    }

#if GPIO_DIAG
    DiagPuts("[GPIO] registered 144 pins\r\n");
#endif
    return HDF_SUCCESS;
}

static void GpioDriverRelease(struct HdfDeviceObject *device)
{
#if GPIO_DIAG
    DiagPuts("\r\n[REL] *** GpioDriverRelease CALLED ***\r\n");
#endif
    GpioCntlrRemove(&g_n32GpioCntlr);

    if (device != NULL) {
        PlatformDeviceUnbind(&g_n32GpioCntlr.device, device);
    }
    PlatformDeviceDestroyService(&g_n32GpioCntlr.device);
}

/*
 * HDF platform driver for <PERIPH> on <CHIP>.
 *
 * Must NOT include vendor device headers: the HDF header chain pulls in soc.h,
 * which declares enum IRQn just like the vendor device header. All register
 * work lives in <chip>_<periph>_hw.c.
 *
 * Service model: if the application opens the device by name (XxxOpen(num)),
 * use policy = 2 and serviceName = "HDF_PLATFORM_<PERIPH>_<num>"; controller
 * style (open by bus id) uses policy = 0 plus a manager node.
 */
#include "device_resource_if.h"
#include "hdf_device_desc.h"
#include "hdf_core_log.h"
#include "<periph>_core.h"          /* i2c_core.h / pwm_core.h / ... */
#include "osal_mem.h"

#include "<chip>_<periph>_hw.h"

#define HDF_LOG_TAG <periph>_<chip>

struct <Chip><Periph>Res {
    uint32_t num;
    uint8_t  port;
    uint32_t arg;
};

static int32_t N32XxxSetConfig(struct <DevType> *dev, struct XxxConfig *cfg);
static int32_t N32XxxOpen(struct <DevType> *dev);
static int32_t N32XxxClose(struct <DevType> *dev);

static struct XxxMethod g_method;

static int32_t N32XxxBind(struct HdfDeviceObject *device)
{
    struct <DevType> *dev = 0;
    if (device == 0) {
        return HDF_ERR_INVALID_PARAM;
    }
    dev = (struct <DevType> *)OsalMemCalloc(sizeof(*dev));
    if (dev == 0) {
        return HDF_ERR_MALLOC_FAIL;
    }
    device->service = &dev->service;
    dev->device = device;
    return HDF_SUCCESS;
}

static int32_t N32XxxInit(struct HdfDeviceObject *device)
{
    struct <DevType> *dev = 0;
    struct <Chip><Periph>Res *res = 0;
    struct DeviceResourceIface *dri = 0;
    int32_t ret;

    if (device == 0 || device->service == 0) {
        return HDF_ERR_INVALID_OBJECT;
    }
    dev = (struct <DevType> *)device->service;

    res = (struct <Chip><Periph>Res *)OsalMemCalloc(sizeof(*res));
    if (res == 0) {
        return HDF_ERR_MALLOC_FAIL;
    }
    res->num = 0; res->port = 0; res->arg = 0;
    if (device->property != 0) {
        dri = DeviceResourceGetIfaceInstance(HDF_CONFIG_SOURCE);
        if (dri != 0 && dri->GetUint32 != 0) {
            (void)dri->GetUint32(device->property, "num",  &res->num,  0);
            (void)dri->GetUint8 (device->property, "port", &res->port, 0);
            (void)dri->GetUint32(device->property, "arg",  &res->arg,  0);
        }
    } else {
        HDF_LOGE("no hcs property: check deviceMatchAttr / match_attr / include");
    }

    if (<Chip><Periph>HwInit(res->port, res->arg) != <CHIP>_<PERIPH>_OK) {
        OsalMemFree(res);
        return HDF_FAILURE;
    }

    g_method.setConfig = N32XxxSetConfig;
    g_method.open      = N32XxxOpen;
    g_method.close     = N32XxxClose;

    dev->num    = res->num;
    dev->method = &g_method;
    (void)XxxSetPriv(dev, res);

    ret = XxxDeviceAdd(device, dev);      /* i2c: I2cCntlrAdd, pwm: PwmDeviceAdd ... */
    if (ret != HDF_SUCCESS) {
        OsalMemFree(res);
        return ret;
    }
    HDF_LOGI("<PERIPH> %u registered (port %u)", res->num, res->port);
    return HDF_SUCCESS;
}

static void N32XxxRelease(struct HdfDeviceObject *device)
{
    struct <DevType> *dev = 0;
    if (device == 0 || device->service == 0) {
        return;
    }
    dev = (struct <DevType> *)device->service;
    XxxDeviceRemove(device, dev);
    if (dev->priv != 0) {
        OsalMemFree(dev->priv);
        dev->priv = 0;
    }
    OsalMemFree(dev);
}

static int32_t N32XxxSetConfig(struct <DevType> *dev, struct XxxConfig *cfg)
{
    struct <Chip><Periph>Res *res = 0;
    if (dev == 0 || cfg == 0) {
        return HDF_ERR_INVALID_PARAM;
    }
    res = (struct <Chip><Periph>Res *)XxxGetPriv(dev);
    if (res == 0) {
        return HDF_ERR_INVALID_OBJECT;
    }
    /* TODO: convert framework units (ns / count) to hardware units here or in
     * the shim, never in two places. Return HDF_FAILURE on timeout. */
    return HDF_SUCCESS;
}

static int32_t N32XxxOpen(struct <DevType> *dev)
{
    return (dev == 0) ? HDF_ERR_INVALID_PARAM : HDF_SUCCESS;
}

static int32_t N32XxxClose(struct <DevType> *dev)
{
    return (dev == 0) ? HDF_ERR_INVALID_PARAM : HDF_SUCCESS;
}

struct HdfDriverEntry g_<Periph>DriverEntry = {
    .moduleVersion = 1,
    .moduleName    = "<VENDOR>_<PERIPH>_MODULE_HDF",   /* must match the hcs, byte for byte */
    .Bind          = N32XxxBind,
    .Init          = N32XxxInit,
    .Release       = N32XxxRelease,
};
HDF_INIT(g_<Periph>DriverEntry);

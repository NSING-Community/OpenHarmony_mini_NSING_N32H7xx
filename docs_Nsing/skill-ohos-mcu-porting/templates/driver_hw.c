/*
 * <CHIP> <PERIPH> hardware shim - the ONLY file that includes vendor headers.
 *
 * Rules:
 *  - pin/AF/clock facts live in the table below, never in the HDF driver;
 *  - encode/decode (shift, mask, unit conversion) happens here, exactly once;
 *  - every wait loop has a bound expressed in iterations that map to < 1 ms.
 */
#include "<chip>_<periph>_hw.h"

#include "<vendor_device_header>.h"     /* e.g. n32h7xx_<periph>.h */
#include "<vendor_rcc_header>.h"
#include "<vendor_gpio_header>.h"

#define <CHIP>_<PERIPH>_SPIN_LIMIT   20000U   /* keep well under 1 ms */

/* One row per supported pin group. A new port or an alternate pin group is a
 * new row, not a code change. Keep the port clock bits in the row so a port
 * cannot be muxed without its clock being enabled. */
struct <Chip><Periph>PinMap {
    uint8_t  port;
    uint32_t gpioClk;
    /* TODO: per-periph pin fields, e.g.
     * GPIO_Module *sclPort; uint16_t sclPin; uint32_t sclAf; */
};

static const struct <Chip><Periph>PinMap g_pinMap[] = {
    { <CHIP>_<PERIPH>_PORT_0, RCC_AHBx_PERIPHEN_GPIOx, /* pins... */ },
};

static const struct <Chip><Periph>PinMap *Lookup(uint8_t port)
{
    uint32_t i;
    for (i = 0U; i < (sizeof(g_pinMap) / sizeof(g_pinMap[0])); i++) {
        if (g_pinMap[i].port == port) {
            return &g_pinMap[i];
        }
    }
    return 0;
}

int32_t <Chip><Periph>HwPinInit(uint8_t port)
{
    const struct <Chip><Periph>PinMap *map = Lookup(port);
    if (map == 0) {
        return <CHIP>_<PERIPH>_ERR;
    }
    /* TODO: enable map->gpioClk and the AFIO/IO-mux clock, then program the
     * alternate function for each pin. Open-drain vs push-pull matters:
     * I2C needs open drain; PWM/SPI need push-pull. */
    return <CHIP>_<PERIPH>_OK;
}

int32_t <Chip><Periph>HwInit(uint8_t port, uint32_t arg)
{
    if (Lookup(port) == 0) {
        return <CHIP>_<PERIPH>_ERR;
    }
    /* TODO: enable the peripheral clock, then its kernel clock source,
     * then program timing/timing-like parameters, then enable. */
    return <CHIP>_<PERIPH>_OK;
}

int32_t <Chip><Periph>HwWrite(uint8_t port, const uint8_t *buf, uint16_t len)
{
    uint32_t spin = 0U;
    (void)port;
    /* TODO: start -> address -> wait flag -> data -> stop.
     * Bound every wait; return <CHIP>_<PERIPH>_TIMEOUT instead of blocking. */
    while (/* flag not set */) {
        if (++spin > <CHIP>_<PERIPH>_SPIN_LIMIT) {
            return <CHIP>_<PERIPH>_TIMEOUT;
        }
    }
    return <CHIP>_<PERIPH>_OK;
}

int32_t <Chip><Periph>HwRead(uint8_t port, uint8_t *buf, uint16_t len)
{
    (void)port; (void)buf; (void)len;
    /* TODO: same shape as write, direction reversed. */
    return <CHIP>_<PERIPH>_OK;
}

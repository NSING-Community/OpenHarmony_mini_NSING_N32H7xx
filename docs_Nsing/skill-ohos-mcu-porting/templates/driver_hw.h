/*
 * <CHIP> <PERIPH> hardware shim - public interface.
 *
 * This header must NOT include any vendor device header. The HDF driver and the
 * application include only this file; all vendor types stay inside the .c.
 */
#ifndef <CHIP>_<PERIPH>_HW_H
#define <CHIP>_<PERIPH>_HW_H

#include <stdint.h>

#define <CHIP>_<PERIPH>_OK        0
#define <CHIP>_<PERIPH>_ERR       (-1)
#define <CHIP>_<PERIPH>_TIMEOUT   (-2)

/* Bus/port identifiers as seen by the framework (not pin numbers). */
#define <CHIP>_<PERIPH>_PORT_0    0U

/**
 * @brief Route the pins used by this port. Call BEFORE the kernel starts:
 *        vendor GPIO helpers must not run from a driver Init().
 */
int32_t <Chip><Periph>HwPinInit(uint8_t port);

/**
 * @brief Enable clocks and program the peripheral. Safe after the kernel.
 */
int32_t <Chip><Periph>HwInit(uint8_t port, uint32_t arg);

/** @brief One transfer/write. Return <CHIP>_<PERIPH>_TIMEOUT on no response. */
int32_t <Chip><Periph>HwWrite(uint8_t port, const uint8_t *buf, uint16_t len);
int32_t <Chip><Periph>HwRead(uint8_t port, uint8_t *buf, uint16_t len);

#endif /* <CHIP>_<PERIPH>_HW_H */

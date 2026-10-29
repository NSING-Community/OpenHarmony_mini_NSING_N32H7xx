/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file n32_i2c_hw.h
 * @brief Minimal I2C hardware shim between the N32H7xx SDK and HDF drivers.
 *
 * WHY THIS FILE EXISTS:
 *   An HDF platform driver cannot include the vendor device header directly.
 *   The HDF header chain always reaches los_config.h -> target_config.h ->
 *   soc.h, and soc.h declares `enum IRQn`. The vendor header n32h7xx.h declares
 *   the SAME enum, and NEITHER declares it behind a guard, so any translation
 *   unit that pulls in both fails with "redeclaration of 'enum IRQn'".
 *
 *   So the register-level work lives here - in a file that includes the SDK
 *   headers but never an HDF header - and drivers only see these prototypes.
 *
 *   This is the same split the board layer already uses ("board answers what the
 *   hardware is, the app answers what the business does"); here it is "the SoC
 *   layer answers how to touch the registers, the HDF driver answers how to wire
 *   up the framework".
 *
 * ADDRESS CONVENTION:
 *   Every function here takes the 7-bit slave address (0x50 for an AT24), which
 *   is also what HDF's struct I2cMsg carries - see i2c_stm32f4xx.c, which shifts
 *   left by one before handing the address to the peripheral. The N32 controller
 *   likewise wants the 8-bit bus byte in SADR, and I2C_ConfigSendAddress() does
 *   not shift for you, so this shim performs the single left shift internally.
 *   Callers never deal with the 0xA0 form.
 */

#ifndef N32_I2C_HW_H
#define N32_I2C_HW_H

#include <stdint.h>

/* Ports this board brings out to headers: I2C1 -> PB6/PB7 (AF11/AF10, the pins
   the SDK I2C_EEPROM example uses and the ones the AT24 is wired to),
   I2C4 -> PD12/PD13 (shared with GTIMA3 CH1/CH2). */
#define N32_I2C_PORT_1      1U
#define N32_I2C_PORT_4      4U

#define N32_I2C_OK          0
#define N32_I2C_ERR         (-1)
#define N32_I2C_TIMEOUT     (-2)
#define N32_I2C_ERR_IDLE    (-3)    /* BUSY never cleared before the transfer */
#define N32_I2C_ERR_DATA    (-4)    /* data phase never became ready */
#define N32_I2C_ERR_STOP    (-5)    /* STOP condition never seen */

/**
 * @brief Read the peripheral's BUSY bit now: 1/0, or -1 if unreachable.
 *        BUSY stuck at 1 with an idle bus points at a missing kernel clock
 *        or a held reset, not at the transfer itself.
 */
int32_t N32I2cHwBusy(uint8_t port);

/**
 * @brief Pre-kernel self test: try one byte on the bus, before the kernel
 *        starts. Returns 0 on success, or a negative step code.
 */
int32_t N32I2cHwSelfTest(uint8_t port);

/**
 * @brief Register snapshot captured at the last failed transfer, so a driver
 *        can print what the peripheral actually looked like at the timeout.
 *        Pass NULL for the values you do not need.
 */
void N32I2cHwFaultSnapshot(uint32_t *ctrl1, uint32_t *ctrl2, uint32_t *stsint);

/** @brief GPIOB mode register (offset 0) at the last failure. */
uint32_t N32I2cHwGpioPmode(void);

/**
 * @brief Bring up one I2C port: bus clock, kernel clock source, pin mux and
 *        the peripheral itself. `speedHz` selects the timing register preset.
 * @return N32_I2C_OK on success, negative otherwise.
 */
int32_t N32I2cHwInit(uint8_t port, uint32_t speedHz);

/** @brief Polled write of `len` bytes to `devAddr` (7-bit address). */
int32_t N32I2cHwWrite(uint8_t port, uint8_t devAddr, const uint8_t *buf, uint16_t len);

/** @brief The SDK example write sequence, copied for comparison (7-bit addr). */
int32_t N32I2cHwDemoWrite(uint8_t port, uint8_t devAddr, uint8_t memAddr,
                          const uint8_t *pBuf, uint8_t nBytes);

/** @brief Polled read of `len` bytes from `devAddr` (7-bit address). */
int32_t N32I2cHwRead(uint8_t port, uint8_t devAddr, uint8_t *buf, uint16_t len);

/**
 * @brief Probe one 7-bit address: START, [addr|W], sample the ACK bit, STOP.
 *        No data bytes are sent.
 *
 * This is the standard bus-scan primitive (i2c-tools does a zero-length write).
 * @return N32_I2C_OK when the slave acknowledged (device present),
 *         N32_I2C_ERR when it did not (NAK), N32_I2C_TIMEOUT if the bus is stuck.
 */
int32_t N32I2cHwProbe(uint8_t port, uint8_t devAddr);

/**
 * @brief Pin mux only (bus clock + alternate function), no peripheral setup.
 *
 * MUST be called before the kernel starts.
 *
 * WHY: the SDK's GPIO_InitPeripheral() walks a table with a 26-byte stride and
 * overruns its destination when invoked from an HDF driver Init() - i.e. from
 * DeviceManagerStart()'s task context. That corrupts the task control blocks
 * and the system halts with an imprecise bus fault (Type 11, FaultAddr
 * 0xabababab). The vendor examples always call it from main(), which is why
 * they work. So the board start-up chain calls this from main() instead, and
 * the driver's Init() no longer touches the pins at all.
 */
int32_t N32I2cHwPinInit(uint8_t port);

/**
 * @brief Result of the PLL3 configuration done by N32I2cHwPinInit().
 *        Kept so a driver can report it: if PLL3 never locked, the I2C
 *        kernel clock has no source and every register access stalls.
 * @return 0 on success, the RCC ErrorStatus otherwise, -1 if never run.
 */
int32_t N32I2cHwPll3Status(void);

/**
 * @brief I2C timing register value selected together with the clock source.
 *        PLL3C (48 MHz) and the bus clock (150 MHz) need different presets,
 *        so the value depends on which source actually came up.
 */
uint32_t N32I2cHwTiming(void);

#endif /* N32_I2C_HW_H */

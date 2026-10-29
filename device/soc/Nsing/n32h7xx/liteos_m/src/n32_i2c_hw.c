/*
 * Copyright (c) 2026 NSING Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file n32_i2c_hw.c
 * @brief Register-level I2C access for N32H7xx. See n32_i2c_hw.h for why this
 *        shim exists (this file may include the SDK headers, drivers may not).
 *
 * Pin / alternate-function assignments follow the SDK I2C_EEPROM example, which
 * was verified working on this board:
 *   PB6  I2C1_SCL = AF11    PB7  I2C1_SDA = AF10
 *   PD12 I2C4_SCL = AF10    PD13 I2C4_SDA = AF8
 *
 * The I2C kernel clock is PLL3C = 48 MHz, set up by N32I2cHwPinInit() before the
 * kernel starts - exactly what the SDK example does, so its timing register value
 * applies verbatim. PLL3 is separate from the M7 core clock configured by
 * RCC_SetSysClkToMode0(), so CPU/AHB/APB rates are unaffected.
 *
 * ADDRESS CONVENTION - the one thing this shim must not get wrong:
 *   Every layer above (HDF's struct I2cMsg, the drivers, the application) uses
 *   the 7-bit slave address, e.g. 0x50 for an AT24. The peripheral does not:
 *   I2C_ConfigSendAddress() stores its argument into CTRL2's SADR field with a
 *   bare mask (`address & I2C_CTRL2_SADR`, 10 bits wide) and NO left shift, so
 *   what lands in SADR must already be the 8-bit bus byte - which is exactly why
 *   the SDK I2C_EEPROM example sends 0xA0 and not 0x50.
 *   Passing 0x50 there puts 7-bit address 0x28 on the wire, nothing answers, and
 *   the address phase is NACKed (NAKF set, WRAVL never asserts, BUSY stuck) -
 *   which is precisely what this driver did before N32_I2C_ADDR_BYTE() existed.
 *   The shift therefore happens here, once, and nowhere else.
 */

#include "n32_i2c_hw.h"

#include "n32h7xx_i2c.h"
#include "n32h7xx_rcc.h"
#include "n32h7xx_gpio.h"

#define N32_I2C_TIMEOUT_LOOP    0x20000U

/* Verbatim from the SDK I2C_EEPROM example: valid for a 48 MHz I2C kernel
   clock (PLL3C). Do not change one without the other. */
#define N32_I2C_TIMING_100K     0x50012526U

/* 7-bit slave address -> the 8-bit bus byte the peripheral expects (see the
   header comment): SADR[7:1] = address, SADR[0] = the R/W bit position, which
   stays 0 because direction is carried by CTRL2's RWN bit instead
   (I2C_DIRECTION_SEND / I2C_DIRECTION_RECV). The SDK example's 0xA0 is 0x50 << 1
   and its receive path reuses the same value with I2C_DIRECTION_RECV, so a
   single shift is all that is needed in both directions. */
#define N32_I2C_ADDR_BYTE(addr7)    ((uint32_t)(addr7) << 1)

static int32_t  g_pll3Status = -1;
static uint32_t g_faultCtrl1 = 0U;
static uint32_t g_faultCtrl2 = 0U;
static uint32_t g_faultStsint = 0U;
static uint32_t g_faultGpioPmode = 0U;
static uint32_t g_timing = 0x50012526U;     /* SDK value for a 48 MHz kernel clock */

/* Forward declaration: this block sits above N32I2cBase()'s definition. */
static I2C_Module *N32I2cBase(uint8_t port);

uint32_t N32I2cHwGpioPmode(void)
{
    return g_faultGpioPmode;
}

void N32I2cHwFaultSnapshot(uint32_t *ctrl1, uint32_t *ctrl2, uint32_t *stsint)
{
    if (ctrl1 != NULL) {
        *ctrl1 = g_faultCtrl1;
    }
    if (ctrl2 != NULL) {
        *ctrl2 = g_faultCtrl2;
    }
    if (stsint != NULL) {
        *stsint = g_faultStsint;
    }
}

int32_t N32I2cHwBusy(uint8_t port)
{
    I2C_Module *base = N32I2cBase(port);

    if (base == NULL) {
        return -1;
    }
    return (I2C_GetFlag(base, I2C_FLAG_BUSY) != RESET) ? 1 : 0;
}

int32_t N32I2cHwPll3Status(void)
{
    return g_pll3Status;
}

uint32_t N32I2cHwTiming(void)
{
    return g_timing;
}

static I2C_Module *N32I2cBase(uint8_t port)
{
    switch (port) {
        case N32_I2C_PORT_1: return (I2C_Module *)I2C1;
        case N32_I2C_PORT_4: return (I2C_Module *)I2C4;
        default:             return NULL;
    }
}

static int32_t N32I2cClockEnable(uint8_t port)
{
    switch (port) {
        case N32_I2C_PORT_1:
            RCC_EnableAPB1PeriphClk4(RCC_APB1_PERIPHEN_M7_I2C1, ENABLE);
            return N32_I2C_OK;
        case N32_I2C_PORT_4:
            RCC_EnableAPB2PeriphClk2(RCC_APB2_PERIPHEN_M7_I2C4, ENABLE);
            return N32_I2C_OK;
        default:
            return N32_I2C_ERR;
    }
}

static int32_t N32I2cKerClkSelect(uint8_t port)
{
    switch (port) {
        case N32_I2C_PORT_1:
            RCC_ConfigI2C1KerClkSource((g_pll3Status != 0) ? RCC_I2CKERCLK_SRC_PLL3C
                                                           : RCC_I2CKERCLK_SRC_HSI);
            return N32_I2C_OK;
        case N32_I2C_PORT_4:
            RCC_ConfigI2C4KerClkSource((g_pll3Status != 0) ? RCC_I2CKERCLK_SRC_PLL3C
                                                           : RCC_I2CKERCLK_SRC_HSI);
            return N32_I2C_OK;
        default:
            return N32_I2C_ERR;
    }
}

/*
 * Pin map.
 *
 * The vendor SDK exposes the mechanism (GPIO_InitPeripheral) but not the
 * policy: which pins carry I2C1 or I2C4 lives in each example's main.h, one
 * copy per example. Consolidating it here keeps the driver and the hcs free of
 * AF numbers, and lets a new port or an alternate pin group be added as one
 * table row instead of a code change.
 *
 * Only combinations verified on this board are listed. The complete set is
 * defined by the device pin-mux table in the datasheet.
 */
struct N32I2cPinMap {
    uint8_t      port;
    uint32_t     gpioClk;       /* AHB5 enable bits, M7 and M4 domains */
    GPIO_Module *sclPort;
    uint16_t     sclPin;
    uint32_t     sclAf;
    GPIO_Module *sdaPort;
    uint16_t     sdaPin;
    uint32_t     sdaAf;
};

static const struct N32I2cPinMap g_i2cPinMap[] = {
    {   N32_I2C_PORT_1,
        RCC_AHB5_PERIPHEN_M7_GPIOB | RCC_AHB5_PERIPHEN_M4_GPIOB,
        GPIOB, GPIO_PIN_6,  GPIO_AF11,
        GPIOB, GPIO_PIN_7,  GPIO_AF10 },    /* SDK I2C_EEPROM example */
    {   N32_I2C_PORT_4,
        RCC_AHB5_PERIPHEN_M7_GPIOD | RCC_AHB5_PERIPHEN_M4_GPIOD,
        GPIOD, GPIO_PIN_12, GPIO_AF10,
        GPIOD, GPIO_PIN_13, GPIO_AF8 },     /* J6 header on this board */
};

static const struct N32I2cPinMap *N32I2cPinLookup(uint8_t port)
{
    uint32_t i;

    for (i = 0U; i < (sizeof(g_i2cPinMap) / sizeof(g_i2cPinMap[0])); i++) {
        if (g_i2cPinMap[i].port == port) {
            return &g_i2cPinMap[i];
        }
    }
    return NULL;
}

int32_t N32I2cHwPinInit(uint8_t port)
{
    const struct N32I2cPinMap *map = N32I2cPinLookup(port);
    GPIO_InitType init;

    if (map == NULL) {
        return N32_I2C_ERR;
    }

    /* I2C kernel clock = PLL3C = 48 MHz, matching the SDK I2C_EEPROM example so
       its timing value applies verbatim. PLL3 is independent of the M7 core
       clock (RCC_SetSysClkToMode0), so configuring it here does not disturb
       the CPU/AHB/APB rates. Must happen before the kernel starts, because
       RCC_WaitHseStable() spins. */
    RCC_ConfigHse(RCC_HSE_ENABLE);
    RCC_WaitHseStable();
    /* HSE_VALUE differs between headers (n32h7xx.h says 25 MHz,
       system_n32h7xx.c says 8 MHz). 25 MHz -> 480 MHz is a ratio of 19.2, which
       RCC_CalculatePLLParam() rejects, so use the 8 MHz figure explicitly: it
       divides into 480 MHz exactly (x60) and the cost of being wrong is only
       that PLL3 fails, in which case we fall back to the bus clock below. */
    /* ErrorStatus is { ERROR = 0, SUCCESS = 1 }: success is the NON-zero value.
       Getting this backwards silently kept us on the fallback clock, and skipped
       RCC_ConfigPLL3CDivider() - which left the I2C kernel clock wrong and made
       WRAVL never assert. */
    g_pll3Status = (int32_t)RCC_ConfigPll3(RCC_PLL_SRC_HSE, HSE_VALUE, 480000000, ENABLE);
    if (g_pll3Status != 0) {
        RCC_ConfigPLL3CDivider(RCC_PLLC_DIV10);   /* 480/10 = 48 MHz */
        g_timing = 0x50012526U;                   /* SDK value for 48 MHz */
    } else {
        /* No PLL3. Fall back to HSI direct - 64 MHz is the one source we know
           is present and running: RCC_SetSysClkToMode0() uses it to drive the
           M7 at 600 MHz. (The bus clock was tried earlier and did not work.)
           PRESC = 0xF -> 16/64MHz = 250 ns;
           SCLH = SCLL = 0x13 -> 20 x 250 ns = 5.0 us each -> 100 kHz. */
        g_timing = 0xF0001313U;
    }

    /* Both the M7 and M4 enable bits, as board_config.h does for the LEDs. */
    RCC_EnableAHB5PeriphClk1(map->gpioClk, ENABLE);
    RCC_EnableAHB5PeriphClk2(RCC_AHB5_PERIPHEN_PWR | RCC_AHB5_PERIPHEN_M7_AFIO, ENABLE);

    GPIO_InitStruct(&init);
    init.GPIO_Mode = GPIO_MODE_AF_OD;           /* I2C requires open drain */
    /* No internal pull: the SDK example does not set one, and I2C relies on the
       board's external pull-ups. (Adding it earlier was a guess and only ever
       made the bus harder to reason about.) */

    init.Pin            = map->sclPin;
    init.GPIO_Alternate = map->sclAf;
    GPIO_InitPeripheral(map->sclPort, &init);

    init.Pin            = map->sdaPin;
    init.GPIO_Alternate = map->sdaAf;
    GPIO_InitPeripheral(map->sdaPort, &init);

    return N32_I2C_OK;
}

static int32_t N32I2cWaitFlag(I2C_Module *base, uint32_t flag)
{
    uint32_t loop = 0;

    while (I2C_GetFlag(base, flag) == RESET) {
        if (++loop > N32_I2C_TIMEOUT_LOOP) {
            return N32_I2C_TIMEOUT;
        }
    }
    return N32_I2C_OK;
}

/** Wait for the bus to go idle before starting a new transfer. */
static int32_t N32I2cWaitIdle(I2C_Module *base)
{
    uint32_t loop = 0;

    while (I2C_GetFlag(base, I2C_FLAG_BUSY)) {
        if (++loop > N32_I2C_TIMEOUT_LOOP) {
            return N32_I2C_TIMEOUT;
        }
    }
    return N32_I2C_OK;
}

static int32_t N32I2cReapStop(I2C_Module *base)
{
    int32_t ret = N32I2cWaitFlag(base, I2C_FLAG_STOPF);

    I2C_ClrFlag(base, I2C_FLAG_STOPF);
    return ret;
}

int32_t N32I2cHwInit(uint8_t port, uint32_t speedHz)
{
    I2C_InitType init;
    I2C_Module   *base = N32I2cBase(port);

    if (base == NULL) {
        return N32_I2C_ERR;
    }
    (void)speedHz;                              /* only the 100 kHz preset exists so far */

    /* Pins are muxed by the board start-up chain (N32I2cHwPinInit) BEFORE the
       kernel starts - see the header for why the SDK GPIO helper must not run
       from a driver Init(). */
    if (N32I2cClockEnable(port) != N32_I2C_OK ||
        N32I2cKerClkSelect(port) != N32_I2C_OK) {
        return N32_I2C_ERR;
    }

    I2C_InitStruct(&init);
    init.Timing           = N32I2cHwTiming();
    init.HSTiming         = 0x0;
    init.OwnAddress1      = 0x0;
    init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    init.OwnAddress2      = 0x0;
    init.OwnAddress2Masks = I2C_ADDRESS2MASK_NONE;
    init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    init.NoStretchMode    = I2C_NOSTRCH_DISABLE;
    I2C_Init(base, &init);
    I2C_Enable(base, ENABLE);

    return N32_I2C_OK;
}

/**
 * @brief Bring one I2C port from "unconfigured" to "usable".
 *
 * Bus clock, kernel clock source, then the peripheral itself - the same order the
 * SDK I2C_EEPROM example uses. Shared by N32I2cHwInit() (post-kernel) and
 * N32I2cHwSelfTest() (pre-kernel) so both are configured identically.
 */
static void N32I2cBringUp(uint8_t port)
{
    I2C_Module  *base = N32I2cBase(port);
    I2C_InitType init;

    if (base == NULL) {
        return;
    }

    (void)N32I2cClockEnable(port);
    (void)N32I2cKerClkSelect(port);

    I2C_InitStruct(&init);
    init.Timing           = g_timing;
    init.HSTiming         = 0x0;
    init.OwnAddress1      = 0x0;
    init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    init.OwnAddress2      = 0x0;
    init.OwnAddress2Masks = I2C_ADDRESS2MASK_NONE;
    init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    init.NoStretchMode    = I2C_NOSTRCH_DISABLE;
    I2C_Init(base, &init);
    I2C_Enable(base, ENABLE);
}

/**
 * @brief Pre-kernel I2C self test.
 *
 * Brings up the port and immediately tries one byte, so we can tell apart two
 * cases that look identical from inside a driver:
 *   - the hardware/clock is fundamentally wrong (this fails too)
 *   - only the "touching I2C after the kernel started" path is wrong
 *     (this passes while the HDF driver still fails)
 *
 * @return  0  WRAVL appeared and a byte went out - the peripheral works
 *         -1  port invalid
 *         -2  BUSY would not clear (not clocked / held in reset)
 *         -3  WRAVL never appeared (address phase never completed)
 */
int32_t N32I2cHwSelfTest(uint8_t port)
{
    I2C_Module *base = N32I2cBase(port);
    uint32_t    spin;

    if (base == NULL) {
        return -1;
    }

    N32I2cBringUp(port);

    spin = 0;
    while (I2C_GetFlag(base, I2C_FLAG_BUSY) != RESET) {
        if (++spin > N32_I2C_TIMEOUT_LOOP) {
            return -2;
        }
    }

    I2C_ConfigSendAddress(base, N32_I2C_ADDR_BYTE(0x50U), I2C_DIRECTION_SEND);
    base->STSINT |= I2C_FLAG_WRE;
    I2C_SetTransferByteNumber(base, 1U);
    I2C_EnableAutomaticEnd(base, ENABLE);
    I2C_GenerateStart(base, ENABLE);

    spin = 0;
    while (I2C_GetFlag(base, I2C_FLAG_WRAVL) == RESET) {
        if (++spin > N32_I2C_TIMEOUT_LOOP) {
            I2C_GenerateStop(base, ENABLE);
            return -3;
        }
    }

    I2C_SendData(base, 0x00U);
    I2C_GenerateStop(base, ENABLE);

    spin = 0;
    while (I2C_GetFlag(base, I2C_FLAG_STOPF) == RESET) {
        if (++spin > N32_I2C_TIMEOUT_LOOP) {
            break;
        }
    }
    I2C_ClrFlag(base, I2C_FLAG_STOPF);

    return 0;
}

/**
 * @brief Byte-for-byte the SDK I2C_EEPROM example's write sequence.
 *
 * Its state machine (eeprom_page_write) is copied here unchanged apart from
 * naming: address setup -> START -> send the memory address -> send the data
 * bytes -> wait for STOP, each step with its own bounded spin. The point is a
 * like-for-like comparison: the SDK demo completes in 167 ms on this board, so
 * if this path also NACKs the difference is environmental, and if it succeeds
 * the fault is in N32I2cHwWrite()'s simplified loop.
 *
 * @return 0 on success; negative identifies the step that failed.
 */
int32_t N32I2cHwDemoWrite(uint8_t port, uint8_t devAddr, uint8_t memAddr,
                          const uint8_t *pBuf, uint8_t nBytes)
{
    I2C_Module *base = N32I2cBase(port);
    uint16_t    timeout;
    uint8_t     i;
    uint8_t     finished = 0U;

    if (base == NULL || pBuf == NULL || nBytes == 0U) {
        return -1;
    }

    I2C_ConfigSendAddress(base, N32_I2C_ADDR_BYTE(devAddr), I2C_DIRECTION_SEND);
    I2C_SetTransferByteNumber(base, (uint8_t)(nBytes + 1U));   /* +1: memory address */
    base->STSINT |= I2C_FLAG_WRE;
    I2C_EnableAutomaticEnd(base, ENABLE);

    timeout = 0U;
    while (I2C_GetFlag(base, I2C_FLAG_BUSY) && (timeout < 20000U)) {
        timeout++;
    }
    if (timeout >= 20000U) {
        return -2;                                             /* bus never idle */
    }

    I2C_GenerateStart(base, ENABLE);

    /* memory address */
    timeout = 0U;
    while ((!I2C_GetFlag(base, I2C_FLAG_WRAVL)) && (timeout < 20000U)) {
        timeout++;
    }
    if (timeout >= 20000U) {
        return -3;
    }
    I2C_SendData(base, memAddr);

    /* data bytes */
    for (i = 0U; i < nBytes; i++) {
        timeout = 0U;
        while ((!I2C_GetFlag(base, I2C_FLAG_WRAVL)) && (timeout < 20000U)) {
            timeout++;
        }
        if (timeout >= 20000U) {
            return -4;
        }
        I2C_SendData(base, pBuf[i]);
    }

    timeout = 0U;
    while ((!I2C_GetFlag(base, I2C_FLAG_STOPF)) && (timeout < 20000U)) {
        timeout++;
    }
    if (timeout >= 20000U) {
        return -5;
    }
    I2C_ClrFlag(base, I2C_FLAG_STOPF);
    (void)finished;

    return 0;
}

int32_t N32I2cHwWrite(uint8_t port, uint8_t devAddr, const uint8_t *buf, uint16_t len)
{
    I2C_Module *base = N32I2cBase(port);
    /* The transfer-count register is 8 bits wide: a length above 255 would be
     * truncated there while the loop still ran len times (256 -> count 0).
     * Reject instead of silently corrupting the transaction. */
    if (len == 0U || len > 255U) {
        return N32_I2C_ERR;
    }

    uint16_t    i;
    int32_t     ret;

    if (base == NULL || buf == NULL || len == 0) {
        return N32_I2C_ERR;
    }
    if (N32I2cWaitIdle(base) != N32_I2C_OK) {
        return N32_I2C_ERR_IDLE;
    }

    /* Order matters, and matches the SDK example: configure the address
       first, then clear the write-data register. */
    I2C_ConfigSendAddress(base, N32_I2C_ADDR_BYTE(devAddr), I2C_DIRECTION_SEND);
    base->STSINT |= I2C_FLAG_WRE;
    I2C_SetTransferByteNumber(base, (uint8_t)len);
    I2C_EnableAutomaticEnd(base, ENABLE);
    I2C_GenerateStart(base, ENABLE);

    for (i = 0; i < len; i++) {
        ret = N32I2cWaitFlag(base, I2C_FLAG_WRAVL);
        if (ret != N32_I2C_OK) {
            g_faultCtrl1  = (uint32_t)base->CTRL1;
            g_faultCtrl2  = (uint32_t)base->CTRL2;
            g_faultStsint = (uint32_t)base->STSINT;
            /* GPIOB mode register (offset 0). Expect bits 12-15 = 0b1010 for
               PB7/PB6 in alternate-function mode; 0 means the mux never stuck. */
            g_faultGpioPmode = (*(volatile uint32_t *)0x58032C00UL);
            I2C_GenerateStop(base, ENABLE);
            return N32_I2C_ERR_DATA;
        }
        I2C_SendData(base, buf[i]);
    }

    return (N32I2cReapStop(base) == N32_I2C_OK) ? N32_I2C_OK : N32_I2C_ERR_STOP;
}

int32_t N32I2cHwRead(uint8_t port, uint8_t devAddr, uint8_t *buf, uint16_t len)
{
    I2C_Module *base = N32I2cBase(port);
    /* The transfer-count register is 8 bits wide: a length above 255 would be
     * truncated there while the loop still ran len times (256 -> count 0).
     * Reject instead of silently corrupting the transaction. */
    if (len == 0U || len > 255U) {
        return N32_I2C_ERR;
    }

    uint16_t    i;
    int32_t     ret;

    if (base == NULL || buf == NULL || len == 0) {
        return N32_I2C_ERR;
    }
    if (N32I2cWaitIdle(base) != N32_I2C_OK) {
        return N32_I2C_TIMEOUT;
    }

    I2C_ConfigSendAddress(base, N32_I2C_ADDR_BYTE(devAddr), I2C_DIRECTION_RECV);
    I2C_SetTransferByteNumber(base, (uint8_t)len);
    I2C_EnableAutomaticEnd(base, ENABLE);
    I2C_GenerateStart(base, ENABLE);

    for (i = 0; i < len; i++) {
        ret = N32I2cWaitFlag(base, I2C_FLAG_RDAVL);
        if (ret != N32_I2C_OK) {
            I2C_GenerateStop(base, ENABLE);
            return ret;
        }
        buf[i] = I2C_RecvData(base);
    }

    return N32I2cReapStop(base);
}

int32_t N32I2cHwProbe(uint8_t port, uint8_t devAddr)
{
    I2C_Module *base = N32I2cBase(port);
    int32_t     ret;
    uint32_t    spin;

    if (base == NULL) {
        return N32_I2C_ERR;
    }
    if (N32I2cWaitIdle(base) != N32_I2C_OK) {
        return N32_I2C_TIMEOUT;
    }

    I2C_ConfigSendAddress(base, N32_I2C_ADDR_BYTE(devAddr), I2C_DIRECTION_SEND);
    I2C_SetTransferByteNumber(base, 1U);
    I2C_GenerateStart(base, ENABLE);

    /* Wait for the address phase to END, not for a guessed delay: ADR asserts
     * after an ACK, NAKF after a NAK. A bare counting loop has no side effect
     * and is removed by -O2, which would sample NAKF too early. */
    for (spin = 0; spin < N32_I2C_TIMEOUT_LOOP; spin++) {
        if (I2C_GetFlag(base, I2C_FLAG_ADR) != RESET ||
            I2C_GetFlag(base, I2C_FLAG_NAKF) != RESET) {
            break;
        }
    }

    if (I2C_GetFlag(base, I2C_FLAG_ADR) != RESET) {
        I2C_ClrFlag(base, I2C_FLAG_ADR);
        ret = N32_I2C_OK;                       /* ACK -> device present */
    } else {
        if (I2C_GetFlag(base, I2C_FLAG_NAKF) != RESET) {
            I2C_ClrFlag(base, I2C_FLAG_NAKF);
        }
        ret = N32_I2C_ERR;                      /* NAK or timeout -> absent */
    }

    I2C_GenerateStop(base, ENABLE);
    (void)N32I2cReapStop(base);
    return ret;
}

/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file board_led.c
 * @brief Tri-color LED control for N32H7xx EVAL.
 *
 * IMPORTANT structural rule (learned from the proven NSING reference port):
 *   Pin configuration (GPIO_InitPeripheral) must happen BEFORE LOS_KernelInit().
 *   After the kernel is running, only direct register writes are used
 *   (GPIOx->POD ^= pin), never GPIO_InitPeripheral() again.
 *
 * BoardLedEarlyOn() therefore does the full pin configuration and is called
 * from main() before the kernel, exactly like the reference LedInit().
 * BoardLedInit() after the kernel only enables clocks and re-applies the
 * output state via direct register writes -- no library struct copy.
 */

#include "board_led.h"

#include "board_config.h"
#include "n32h7xx_gpio.h"
#include "n32h7xx_rcc.h"

extern void BootPutsRaw(const char *s);

#define STEP(msg)   do { BootPutsRaw(msg); } while (0)

/* Direct GPIO port bit-set/reset registers (N32H7xx). Using these avoids
   calling into the std_periph library after the kernel has started. */
#define GPIO_POD_OFFSET    0x18U   /* port output data */

static void DemoLedConfig(GPIO_Module *port, uint16_t pin)
{
    GPIO_InitType gpio;

    GPIO_InitStruct(&gpio);
    gpio.Pin       = pin;
    gpio.GPIO_Mode = GPIO_MODE_OUTPUT_PP;
    gpio.GPIO_Pull = GPIO_NO_PULL;
    GPIO_InitPeripheral(port, &gpio);
}

/* Configure all three LED pins. MUST be called before LOS_KernelInit(). */
static void BoardLedConfigAll(void)
{
    BOARD_LED_RED_CLK_ENABLE();
    DemoLedConfig(BOARD_LED_RED_PORT, BOARD_LED_RED_PIN);
    BOARD_LED_GREEN_CLK_ENABLE();
    DemoLedConfig(BOARD_LED_GREEN_PORT, BOARD_LED_GREEN_PIN);
    BOARD_LED_BLUE_CLK_ENABLE();
    DemoLedConfig(BOARD_LED_BLUE_PORT, BOARD_LED_BLUE_PIN);
}

void BoardLedEarlyOn(void)
{
    /* Configure ALL LED pins now (before the kernel) so nothing here has to be
       re-done with a library call once the scheduler is up. Then drive the
       red LED on with a direct register write. */
    BoardLedConfigAll();

    /* Common-anode LEDs: LOW = ON. Clear PB3 -> red on. */
    BOARD_LED_RED_PORT->POD &= ~(uint32_t)BOARD_LED_RED_PIN;
}

void BoardLedInit(void)
{
    /* Post-kernel: clocks are already enabled and pins already configured by
       BoardLedEarlyOn(). Only re-assert the output latch state. No
       GPIO_InitPeripheral() here -- see file header. */
    BOARD_LED_RED_PORT->POD   |= (uint32_t)BOARD_LED_RED_PIN;
    BOARD_LED_GREEN_PORT->POD |= (uint32_t)BOARD_LED_GREEN_PIN;
    BOARD_LED_BLUE_PORT->POD  |= (uint32_t)BOARD_LED_BLUE_PIN;
}

void BoardLedSet(BoardLedId id, bool on)
{
    GPIO_Module *port;
    uint16_t pin;

    switch (id) {
        case BOARD_LED_RED:   port = BOARD_LED_RED_PORT;   pin = BOARD_LED_RED_PIN;   break;
        case BOARD_LED_GREEN: port = BOARD_LED_GREEN_PORT; pin = BOARD_LED_GREEN_PIN; break;
        case BOARD_LED_BLUE:  port = BOARD_LED_BLUE_PORT;  pin = BOARD_LED_BLUE_PIN;  break;
        default: return;
    }

    if (on) {
        port->POD &= ~(uint32_t)pin;   /* LOW = ON (common anode) */
    } else {
        port->POD |= (uint32_t)pin;
    }
}

void BoardLedToggle(BoardLedId id)
{
    switch (id) {
        case BOARD_LED_RED:   BOARD_LED_RED_PORT->POD   ^= (uint32_t)BOARD_LED_RED_PIN;   break;
        case BOARD_LED_GREEN: BOARD_LED_GREEN_PORT->POD ^= (uint32_t)BOARD_LED_GREEN_PIN; break;
        case BOARD_LED_BLUE:  BOARD_LED_BLUE_PORT->POD  ^= (uint32_t)BOARD_LED_BLUE_PIN;  break;
        default: break;
    }
}

void BoardLedAllOff(void)
{
    BOARD_LED_RED_PORT->POD   |= (uint32_t)BOARD_LED_RED_PIN;
    BOARD_LED_GREEN_PORT->POD |= (uint32_t)BOARD_LED_GREEN_PIN;
    BOARD_LED_BLUE_PORT->POD  |= (uint32_t)BOARD_LED_BLUE_PIN;
}

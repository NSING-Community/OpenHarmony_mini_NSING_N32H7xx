/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file board_config.h
 * @brief Board-level configuration for N32H7xx EVAL (OpenHarmony LiteOS-M mini).
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "n32h7xx.h"

/* Console: USART1 on CM7 (from SDK log.c, CM7 branch). */
#define BOARD_CONSOLE_USART        USART1
#define BOARD_CONSOLE_BAUDRATE     (115200)  /* matches NSING SDK bsp/src/log.c CM7 branch (USART1 PA9/PA10) */
#define BOARD_CONSOLE_TX_PORT      GPIOA
#define BOARD_CONSOLE_TX_PIN       GPIO_PIN_9
#define BOARD_CONSOLE_TX_AF        ((uint8_t)0x07)
#define BOARD_CONSOLE_RX_PORT      GPIOA
#define BOARD_CONSOLE_RX_PIN       GPIO_PIN_10
#define BOARD_CONSOLE_RX_AF        ((uint8_t)0x05)

/* GPIOA clock for the console, SDK pattern: enable BOTH M7 and M4 periph bits. */
#define BOARD_CONSOLE_CLOCK        (RCC_AHB5_PERIPHEN_M7_GPIOA | RCC_AHB5_PERIPHEN_M4_GPIOA)
#define BOARD_CONSOLE_CLOCK_ENABLE() RCC_EnableAHB5PeriphClk1(BOARD_CONSOLE_CLOCK, ENABLE)

/* Clock source selection used by the SDK example main (RCC_SetSysClkToMode0). */
#define BOARD_SYS_CLK_MODE         0

/* Tri-color LEDs, common-anode: VCC3.3 -> 4.7K -> LED -> pin, LOW = ON.
   Wiring per EVAL schematic (LED page): LED1_RED=PB3, LED3_GREEN=PF10, LED2_BLUE=PI8.
   Clock pattern follows the SDK LedBlink example (N32H787_HMI branch):
   enable BOTH M7 and M4 periph bits; PB3/PF10 on AHB5 Clk1, PI8 on Clk2. */
#define BOARD_LED_RED_PORT         GPIOB
#define BOARD_LED_RED_PIN          GPIO_PIN_3
#define BOARD_LED_RED_CLK          (RCC_AHB5_PERIPHEN_M7_GPIOB | RCC_AHB5_PERIPHEN_M4_GPIOB)
#define BOARD_LED_RED_CLK_ENABLE()   RCC_EnableAHB5PeriphClk1(BOARD_LED_RED_CLK, ENABLE)

/* Board-level aliases matching the NSING SDK naming (main.h in the bare-metal
   examples): LED2 = the red LED on PB3. Used by the early boot banner path. */
#define LED2_PORT                  BOARD_LED_RED_PORT
#define LED2_PIN                   BOARD_LED_RED_PIN
#define LED2_CLOCK                 BOARD_LED_RED_CLK

#define BOARD_LED_GREEN_PORT       GPIOF
#define BOARD_LED_GREEN_PIN        GPIO_PIN_10
#define BOARD_LED_GREEN_CLK        (RCC_AHB5_PERIPHEN_M7_GPIOF | RCC_AHB5_PERIPHEN_M4_GPIOF)
#define BOARD_LED_GREEN_CLK_ENABLE() RCC_EnableAHB5PeriphClk1(BOARD_LED_GREEN_CLK, ENABLE)

#define BOARD_LED_BLUE_PORT        GPIOI
#define BOARD_LED_BLUE_PIN         GPIO_PIN_8
#define BOARD_LED_BLUE_CLK         (RCC_AHB5_PERIPHEN_M7_GPIOI | RCC_AHB5_PERIPHEN_M4_GPIOI)
#define BOARD_LED_BLUE_CLK_ENABLE()  RCC_EnableAHB5PeriphClk2(BOARD_LED_BLUE_CLK, ENABLE)

#endif /* BOARD_CONFIG_H */

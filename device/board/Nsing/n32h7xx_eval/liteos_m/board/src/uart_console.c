/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file uart_console.c
 * @brief Early UART console for N32H7xx EVAL (USART1 @ 115200, PA9/PA10).
 *
 * This is the minimal polled console used during boot and by printf(). It is a
 * straight adaptation of the NSING SDK bsp/src/log.c (CM7 branch) but uses the
 * standard peripheral driver API so it links with the rest of the SoC code.
 *
 * Once hilog_lite is up, the same USART can be registered as the OHOS console;
 * until then, printf()/_write() route here so early logs are visible.
 */

#include "board_config.h"
#include "n32h7xx_gpio.h"
#include "n32h7xx_usart.h"
#include "n32h7xx_rcc.h"

#include <stdio.h>
#include <string.h>

#ifndef CHR_CR
#define CHR_CR 0x0D
#endif
#ifndef CHR_LF
#define CHR_LF 0x0A
#endif

void BoardClockInit(void)
{
    /* SDK Mode0: M7 = 600 MHz (PLL1A from HSI), AHB = 300 MHz, APB = 150 MHz.
       Must run before UART baud config so LiteOS tick matches OS_SYS_CLOCK=600000000. */
    RCC_SetSysClkToMode0();

    /* Release the Cortex-M4 core from reset and set its vector base.
       EVERY NSING N32H78x example does this right after the clock switch,
       including the RT-Thread Nano CM7 port. Without it the M4 stays held in
       reset, which stalls the M7 on shared RCC/bus-matrix accesses and makes
       the PLL-lock spin inside RCC_SetSysClkToMode0 never complete -> total
       silence (no UART, no LED). CM4_BaseAddr = FLASH_BANK2 (0x15080000). */
    /* Releasing the CM4 reset is mandatory: while it is held, M7 accesses to
     * shared peripherals stall (symptom: no console output at all). CM4 gets no
     * image, and 0x15080000 lies inside the M7 FLASH region
     * [0x15000000, 0x151E0000) - see board.ld. The M7 image must stay below
     * 0x80000 (512 KB); it is currently about 160 KB. */
    RCC_EnableCM4(0x15080000);
}

void UartInit(void)
{
    GPIO_InitType GPIO_InitStructure;
    USART_InitType USART_InitStructure;

    /* Enable GPIOA (AHB5, M7) and USART1 (APB1, M7) clocks. */
    /* SDK pattern: GPIOA clock for both M7 and M4. */
    BOARD_CONSOLE_CLOCK_ENABLE();
    RCC_EnableAPB1PeriphClk3(RCC_APB1_PERIPHEN_M7_USART1, ENABLE);

    GPIO_InitStruct(&GPIO_InitStructure);

    GPIO_InitStructure.Pin       = BOARD_CONSOLE_TX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.GPIO_Pull = GPIO_NO_PULL;
    GPIO_InitStructure.GPIO_Alternate = BOARD_CONSOLE_TX_AF;
    GPIO_InitPeripheral(BOARD_CONSOLE_TX_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin       = BOARD_CONSOLE_RX_PIN;
    GPIO_InitStructure.GPIO_Alternate = BOARD_CONSOLE_RX_AF;
    GPIO_InitPeripheral(BOARD_CONSOLE_RX_PORT, &GPIO_InitStructure);

    USART_StructInit(&USART_InitStructure);
    USART_InitStructure.BaudRate = BOARD_CONSOLE_BAUDRATE;
    USART_InitStructure.WordLength = USART_WL_8B;
    USART_InitStructure.StopBits = USART_STPB_1;
    USART_InitStructure.Parity = USART_PE_NO;
    USART_InitStructure.HardwareFlowControl = USART_HFCTRL_NONE;
    USART_InitStructure.Mode = USART_MODE_RX | USART_MODE_TX;
    USART_Init(BOARD_CONSOLE_USART, &USART_InitStructure);

    USART_Enable(BOARD_CONSOLE_USART, ENABLE);
}

/* Mirrors the NSING SDK bsp/src/log.c fputc() LF->CRLF state machine.
   IMPORTANT: wait on TXDE (data register EMPTY), NOT TXC (transmission
   COMPLETE). TXC is only set after a full frame has left the shift register,
   so polling it before the first byte of a burst spins forever -- that is what
   truncated the boot banner. */
static int g_crSent = 0;

static void UartPutcRaw(int ch)
{
    if (ch == CHR_CR) {
        g_crSent = 1;
    } else if (ch == CHR_LF) {
        if (!g_crSent) {
            while (USART_GetFlagStatus(BOARD_CONSOLE_USART, USART_FLAG_TXDE) == RESET) {
            }
            USART_SendData(BOARD_CONSOLE_USART, (uint8_t)CHR_CR);
        }
        g_crSent = 0;
    } else {
        g_crSent = 0;
    }

    while (USART_GetFlagStatus(BOARD_CONSOLE_USART, USART_FLAG_TXDE) == RESET) {
    }
    USART_SendData(BOARD_CONSOLE_USART, (uint8_t)ch);
}

void UartPutc(int ch)
{
    UartPutcRaw(ch);
}

int uart_write(const char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        UartPutcRaw(buf[i]);
    }
    return (int)len;
}

/* newlib syscall: route printf()/puts() through the UART console. */
int _write(int fd, char *ptr, int len)
{
    (void)fd;
    if (len <= 0 || ptr == NULL) {
        return 0;
    }
    return uart_write(ptr, (size_t)len);
}

/* Weak fputc for toolchains that route stdio through fputc. */
int fputc(int ch, FILE *f)
{
    (void)f;
    UartPutcRaw(ch);
    return ch;
}

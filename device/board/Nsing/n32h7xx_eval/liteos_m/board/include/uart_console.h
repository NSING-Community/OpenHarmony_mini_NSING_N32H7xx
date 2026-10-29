/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file uart_console.h
 * @brief Early polled UART console for N32H7xx EVAL (USART1 @ 115200, PA9/PA10).
 */

#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void BoardClockInit(void);
void UartInit(void);
void UartPutc(int ch);
int uart_write(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* UART_CONSOLE_H */

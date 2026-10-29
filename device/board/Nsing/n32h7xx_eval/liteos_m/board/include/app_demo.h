/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file app_demo.h
 * @brief Board demo: two LiteOS-M tasks (LED blinker + heartbeat logger).
 */

#ifndef APP_DEMO_H
#define APP_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the demo tasks.
 *
 * Must be called AFTER LOS_KernelInit() and BEFORE LOS_Start(), because
 * LiteOS-M only allows task creation once the kernel is initialised.
 */
void BoardDemoInit(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DEMO_H */

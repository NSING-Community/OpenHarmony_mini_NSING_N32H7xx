/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file app_demo.c
 * @brief Board demo: two LiteOS-M tasks.
 *
 *   led_task (prio 6) - toggles the user LED every 500 ms
 *   log_task (prio 7) - prints a heartbeat line every 1000 ms
 *
 * Called from main() between LOS_KernelInit() and LOS_Start().
 *
 * NOTE: this file must NOT include the N32H7xx device header. los_config.h
 * pulls in target_config.h -> soc.h, which defines enum IRQn; the device
 * header defines the same enum, so having both in one translation unit is a
 * redeclaration error. LED access goes through board_led.h instead.
 */

#include "app_demo.h"

#include "los_config.h"
#include "los_debug.h"
#include "los_task.h"

#include "board_led.h"

/* DEBUG PROBE: mark each step of BoardDemoInit so an exception can be pinned
   to a single LED configuration. Remove once the fault is fixed. */
#define STEP(msg) do { BootPutsRaw(msg); } while (0)

extern void BootPutsRaw(const char *s);

#ifndef LOS_MS2Tick
#define LOS_MS2Tick(ms) ((UINT32)((ms) * LOSCFG_BASE_CORE_TICK_PER_SECOND / 1000))
#endif

#define DEMO_TASK_STACK_SIZE 0x1000
#define DEMO_LED_PRIO        6
#define DEMO_LOG_PRIO        7
#define DEMO_LED_PERIOD_MS   400
#define DEMO_LOG_PERIOD_MS   1000

static UINT32 g_ledTaskId;
static UINT32 g_logTaskId;

static UINT32 DemoLedTask(UINT32 arg)
{
    BoardLedId led = BOARD_LED_RED;

    (void)arg;
    while (1) {
        BoardLedAllOff();
        BoardLedSet(led, true);
        led = (BoardLedId)((led + 1) % BOARD_LED_COUNT);
        LOS_TaskDelay(LOS_MS2Tick(DEMO_LED_PERIOD_MS));
    }
}

static UINT32 DemoLogTask(UINT32 arg)
{
    UINT32 count = 0;

    (void)arg;
    while (1) {
        PRINT_RELEASE("[N32H7] demo: heartbeat %u - led toggling\r\n", count++);
        LOS_TaskDelay(LOS_MS2Tick(DEMO_LOG_PERIOD_MS));
    }
}

static UINT32 DemoCreateTask(UINT32 *id, TSK_ENTRY_FUNC entry, UINT16 prio, const char *name)
{
    TSK_INIT_PARAM_S param = {0};
    UINT32 ret;

    param.pfnTaskEntry = entry;
    param.usTaskPrio   = prio;
    param.uwArg        = 0;
    param.stackAddr    = 0;
    param.uwStackSize  = DEMO_TASK_STACK_SIZE;
    param.pcName       = (CHAR *)name;
    param.uwResved     = 0; /* detached */

    ret = LOS_TaskCreate(id, &param);
    if (ret != LOS_OK) {
        PRINT_RELEASE("[N32H7] create %s failed: 0x%x\r\n", name, ret);
    }
    return ret;
}

void BoardDemoInit(void)
{
    STEP("[N32H7] demo: led init start\r\n");
    BoardLedInit();
    STEP("[N32H7] demo: led init done\r\n");

    STEP("[N32H7] demo: create led_task\r\n");
    (void)DemoCreateTask(&g_ledTaskId, DemoLedTask, DEMO_LED_PRIO, "led_task");
    STEP("[N32H7] demo: create log_task\r\n");
    (void)DemoCreateTask(&g_logTaskId, DemoLogTask, DEMO_LOG_PRIO, "log_task");
    STEP("[N32H7] demo: tasks created\r\n");
}

/*
 * Copyright (c) 2026 NSING Technologies (formerly Nations). All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file soc.h
 * @brief Minimal CMSIS view required by the LiteOS-M ARM Cortex-M7 arch layer.
 *
 * LiteOS-M arch code (los_timer.c / los_common_interrupt.c) needs IRQn_Type,
 * SysTick_IRQn and SysTick_Config. The full N32H7xx device header is NOT pulled
 * in here on purpose: it defines register members such as `SA`, which collide
 * with macros of the same name in third_party/musl. Board/SoC driver sources
 * include the device header directly instead.
 */

#ifndef _SOC_H
#define _SOC_H

typedef enum IRQn {
    NonMaskableInt_IRQn   = -14,
    HardFault_IRQn        = -13,
    MemoryManagement_IRQn = -12,
    BusFault_IRQn         = -11,
    UsageFault_IRQn       = -10,
    SVCall_IRQn           = -5,
    DebugMonitor_IRQn     = -4,
    PendSV_IRQn           = -2,
    SysTick_IRQn          = -1,
} IRQn_Type;

#define __CM7_REV             0x0000U
#define __FPU_PRESENT         1U
#define __MPU_PRESENT         1U
#define __ICACHE_PRESENT      1U
#define __DCACHE_PRESENT      1U
#define __NVIC_PRIO_BITS      4U
#define __Vendor_SysTickConfig 0U

#include "core_cm7.h"

#endif /* _SOC_H */

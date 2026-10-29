/*
 * Copyright (c) 2026 NSING Technologies (formerly Nations). All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * LiteOS-M target configuration for N32H7xx (N32H76x: Cortex-M7, VFP-D16).
 * The heap region is exported by device/soc/Nsing/n32h7xx/liteos_m/board.ld.
 */

#ifndef _TARGET_CONFIG_H
#define _TARGET_CONFIG_H

#include "stdbool.h"

/* N32H7xx device header: IRQn_Type, SysTick_IRQn, and (via core_cm7.h)
   SysTick_Config. LiteOS-M arch code needs these through los_config.h. */
#include "soc.h"

/*=============================================================================
                                        System clock module configuration
=============================================================================*/
/* OS_SYS_CLOCK also comes from board_cflags (-DOS_SYS_CLOCK=...). */
#ifndef OS_SYS_CLOCK
#define OS_SYS_CLOCK                                        (600000000UL)
#endif
/* 100 Hz = 10 ms per tick, the LiteOS-M / mini default. Keep this as the single
 * source of truth and keep the product kernel configs in sync with it: a mismatch
 * makes a delay written as a bare tick count silently change meaning.
 * Application delays must use MsToTicks(), never a raw tick count. */
#define LOSCFG_BASE_CORE_TICK_PER_SECOND                    (100UL)
#define LOSCFG_BASE_CORE_TICK_HW_TIME                       0
#define LOSCFG_BASE_CORE_TICK_WTIMER                        0
#define LOSCFG_BASE_CORE_TICK_RESPONSE_MAX                  0xFFFFFFUL

/*=============================================================================
                                        Hardware interrupt module configuration
=============================================================================*/
#define LOSCFG_PLATFORM_HWI                                 1
#define LOSCFG_USE_SYSTEM_DEFINED_INTERRUPT                 1
#define LOSCFG_PLATFORM_HWI_LIMIT                           416

/*
 * RAM vector table alignment (g_hwiForm).
 *
 * HalHwiInit() does: SCB->VTOR = (UINT32)g_hwiForm;
 * The Cortex-M7 requires VTOR to be aligned to the vector table size rounded
 * up to the next power of two (min 128 bytes).  Our table is
 *   OS_VECTOR_CNT = OS_SYS_VECTOR_CNT(16) + LOSCFG_PLATFORM_HWI_LIMIT(416) = 432
 *   -> 432 * 4 = 1728 bytes -> next power of two = 2048 (0x800).
 *
 * The los_config.h default is 0x100 (256), which is TOO SMALL: the core then
 * ignores the low VTOR bits when dispatching an exception and fetches vectors
 * from the wrong base (0x24000000), landing in garbage and raising an imprecise
 * bus fault (OS_EXC_BF_IMPRECISERR, Type=11, FaultAddr=0xABABABAB).  That is
 * exactly the crash seen right after the kernel enables BUSFAULT/MEMFAULT.
 *
 * MUST stay >= the power-of-two rounding of OS_VECTOR_CNT*4.  If you change
 * LOSCFG_PLATFORM_HWI_LIMIT, re-check this value.
 */
#define LOSCFG_ARCH_HWI_VECTOR_ALIGN                        0x800

/*=============================================================================
                                       Task module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_TSK_LIMIT                          24
#define LOSCFG_BASE_CORE_TSK_IDLE_STACK_SIZE                (0x500U)
#define LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE             (0x1000U)
#define LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE                 (0x130U)
#define LOSCFG_BASE_CORE_TIMESLICE                          1
#define LOSCFG_BASE_CORE_TIMESLICE_TIMEOUT                  20000
#define LOSCFG_BASE_CORE_TSK_MONITOR                        1
#define LOSCFG_BASE_CORE_EXC_TSK_SWITCH                     1

/*=============================================================================
                                       Semaphore module configuration
=============================================================================*/
#define LOSCFG_BASE_IPC_SEM                                 1
#define LOSCFG_BASE_IPC_SEM_LIMIT                           128
/*=============================================================================
                                       Mutex module configuration
=============================================================================*/
#define LOSCFG_BASE_IPC_MUX                                 1
#define LOSCFG_BASE_IPC_MUX_LIMIT                           256
/*=============================================================================
                                       Queue module configuration
=============================================================================*/
#define LOSCFG_BASE_IPC_QUEUE                               1
#define LOSCFG_BASE_IPC_QUEUE_LIMIT                         24
/*=============================================================================
                                       Software timer module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_SWTMR                              1
#define LOSCFG_BASE_CORE_SWTMR_ALIGN                        1
#define LOSCFG_BASE_CORE_SWTMR_LIMIT                        48

/*=============================================================================
                                       Memory module configuration
=============================================================================*/
extern unsigned int __heap_start;
extern unsigned int __heap_end;
#define LOSCFG_SYS_EXTERNAL_HEAP                            1
#define LOSCFG_SYS_HEAP_ADDR                                (void *)&__heap_start
#define LOSCFG_SYS_HEAP_SIZE \
    ((unsigned int)&__heap_end - (unsigned int)&__heap_start)
#define LOSCFG_MEM_MUL_POOL                                 1
#define OS_SYS_MEM_NUM                                      20
#define LOSCFG_MEM_FREE_BY_TASKID                           1
#define LOSCFG_MEMORY_BESTFIT                               1

/* =============================================================================
                                       printf module configuration
============================================================================= */
#define LOSCFG_KERNEL_PRINTF                                1

#define LOS_KERNEL_TEST_NOT_SMOKE                           0

/*=============================================================================
                                       shell module configuration
=============================================================================*/
#define LOSCFG_USE_SHELL                                    1
#define LOSCFG_SHELL_PRIO                                   3

#endif /* _TARGET_CONFIG_H */

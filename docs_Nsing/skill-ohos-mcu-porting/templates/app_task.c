/*
 * Demo task for <PERIPH>. Registered with SYS_RUN: every SYS_RUN entry in the
 * image is called once by MODULE_INIT(run), so multiple demos all run - they are
 * told apart by task name, priority, period and log prefix, not by SYS_RUN.
 *
 * Build for XTS with --gn-args app_demo=false to compile this out entirely.
 */
#include <stdarg.h>
#include <stdio.h>

#include "ohos_init.h"
#include "cmsis_os2.h"
#include "uart_if.h"
#include "<periph>_if.h"        /* platform interface only, no vendor headers */

#ifndef APP_DEMO_TASKS
#define APP_DEMO_TASKS 1
#endif

#define APP_<PERIPH>_PRIO      5      /* low: a slow sensor should never preempt UI/heartbeat */
#define APP_<PERIPH>_PERIOD_MS 1000U

extern DevHandle g_uartHandle;
extern void UartWrite(DevHandle h, const unsigned char *buf, unsigned int len);

static void AppLogf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);   /* board formatter: no heap, no FILE buffer */
    va_end(ap);
    if (n > 0) {
        UartWrite(g_uartHandle, (const unsigned char *)buf, (unsigned int)n);
    }
}

static void *App<Periph>Task(void *arg)
{
    DevHandle h = 0;
    (void)arg;

    osDelay(MsToTicks(1500));                  /* let the console settle */
    h = <Periph>Open(0);
    if (h == 0) {
        AppLogf("[<PERIPH>] open failed\n");
        return 0;
    }
    AppLogf("[<PERIPH>] opened\n");

    while (1) {
        /* TODO: use the platform interface, then print something that can be
         * judged at a glance (value + raw code + error code). */
        osDelay(MsToTicks(APP_<PERIPH>_PERIOD_MS));
    }
}

static void App<Periph>Entry(void)
{
    if (AppCreateTask((osThreadFunc_t)App<Periph>Task,
                      (osPriority_t)APP_<PERIPH>_PRIO, "app_<periph>") == 0) {
        AppLogf("[<PERIPH>] task create failed\n");
    }
}

#if APP_DEMO_TASKS
SYS_RUN(App<Periph>Entry);
#endif

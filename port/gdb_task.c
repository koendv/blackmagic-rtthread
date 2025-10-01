/*
 * gdb server thread.
 */

#include <rtthread.h>

#include "general.h"
#include "platform.h"

#ifndef BLACKMAGIC_CUSTOM_GDB_TASK

#include "gdb_if.h"
#include "gdb_main.h"
#include "gdb_task.h"
#include "target.h"
#include "exception.h"
#include "gdb_packet.h"
#include "memwatch.h"
#include "usb_cdc.h"
#ifdef ENABLE_RTT
#include "rtt.h"
#endif

#ifndef GDB_STACK_SIZE
#define GDB_STACK_SIZE 4096
#endif

#ifndef GDB_PRIORITY
#define GDB_PRIORITY 20
#endif

#ifndef GDB_TICK
#define GDB_TICK 20
#endif

static rt_thread_t gdb_server_thread = RT_NULL;
static rt_sem_t    gdb_server_sem    = RT_NULL;

static void bmp_poll_loop(void)
{
    SET_IDLE_STATE(false);
    while (gdb_target_running && cur_target)
    {
        gdb_poll_target();
        // Check again, as `gdb_poll_target()` may
        // alter these variables.
        if (!gdb_target_running || !cur_target)
            break;
        char c = gdb_if_getchar_to(0);
        if (c == '\x03' || c == '\x04')
            target_halt_request(cur_target);
#ifdef ENABLE_RTT
        if (rtt_enabled)
            poll_rtt(cur_target);
#endif
#ifdef ENABLE_MEMWATCH
        if (memwatch_cnt != 0)
            poll_memwatch(cur_target);
#endif
        rt_thread_yield();
    }

    SET_IDLE_STATE(true);
    const gdb_packet_s * const packet = gdb_packet_receive();
    // If port closed and target detached, stay idle
    if (packet->data[0] != '\x04' || cur_target)
        SET_IDLE_STATE(false);
    gdb_main(packet);
}

void gdb_server_task()
{
    platform_init();
    while (1)
    {
        rt_sem_take(gdb_server_sem, RT_WAITING_FOREVER);
        TRY(EXCEPTION_ALL)
        {
            while (cdc0_connected())
                bmp_poll_loop();
        }
        CATCH()
        {
        default:
            gdb_put_packet_error(0xffU);
            target_list_free();
            gdb_outf("Uncaught exception: %s\n", exception_frame.msg);
        }
    }
}

void cdc0_set_dtr(bool dtr)
{
    if (dtr)
        rt_sem_release(gdb_server_sem);
}

int app_gdb_server_init(void)
{
    gdb_server_sem    = rt_sem_create("gdb server", 0, RT_IPC_FLAG_FIFO);
    gdb_server_thread = rt_thread_create("gdb server",
                                         gdb_server_task,
                                         RT_NULL,
                                         GDB_STACK_SIZE,
                                         GDB_PRIORITY,
                                         GDB_TICK);
    if (gdb_server_thread != RT_NULL)
        rt_thread_startup(gdb_server_thread);
}

#ifndef NO_INIT_GDB_SERVER
INIT_APP_EXPORT(app_gdb_server_init);
#endif

#endif

/*
 * gdb server thread.
 */

#include <rtthread.h>

#include "general.h"
#include "platform.h"

#ifndef CUSTOM_GDB_TASK

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

static rt_thread_t gdb_server_thread = RT_NULL;

static void bmp_poll_loop(void)
{
	SET_IDLE_STATE(false);
	while (gdb_target_running && cur_target) {
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
		if (memwatch_cnt != 0)
			poll_memwatch(cur_target);
		rt_thread_yield();
	}

	SET_IDLE_STATE(true);
	const gdb_packet_s *const packet = gdb_packet_receive();
	// If port closed and target detached, stay idle
	if (packet->data[0] != '\x04' || cur_target)
		SET_IDLE_STATE(false);
	gdb_main(packet);
}

void gdb_server_task()
{
	platform_init();
	while (1) {
		cdc0_wait_for_dtr();
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

int app_gdb_server_init(void)
{
	gdb_server_thread = rt_thread_create("gdb server", gdb_server_task, RT_NULL, 8192, 4, 10);
	if (gdb_server_thread != RT_NULL)
		rt_thread_startup(gdb_server_thread);
}

#ifndef NO_INIT_GDB_SERVER
INIT_APP_EXPORT(app_gdb_server_init);
#endif

#endif

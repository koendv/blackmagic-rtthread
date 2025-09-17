#include "general.h"
#include "platform.h"

#ifndef CUSTOM_GDB_IF

#include "usb_desc.h"
#include "usb_cdc.h"
#include "gdb_packet.h"
#include "gdb_main.h"
#include "gdb_if.h"
#include "gdb_task.h"
#include <rtthread.h>

static uint8_t  gdb_write_buffer[CDC_MAX_MPS - 1];
static uint32_t gdb_write_idx = 0;

/* read one character from gdb port, no time-out */

char gdb_if_getchar()
{
    if (!cdc0_connected())
    {
        gdb_write_idx = 0;
        return '\x04';
    }
    return cdc0_getchar_timeout(RT_WAITING_FOREVER);
}

/* read one character from gdb port with time-out */

char gdb_if_getchar_to(uint32_t timeout_ms)
{
    if (!cdc0_connected())
    {
        gdb_write_idx = 0;
        return '\x04';
    }
    return cdc0_getchar_timeout(timeout_ms);
}

/* write one character to gdb server port. send usb packet if "flush" */

void gdb_if_putchar(char c, bool flush)
{
    gdb_write_buffer[gdb_write_idx++] = c;
    if (flush || gdb_write_idx == sizeof(gdb_write_buffer))
    {
        cdc0_write(gdb_write_buffer, gdb_write_idx);
        gdb_write_idx = 0;
    }
}

#endif

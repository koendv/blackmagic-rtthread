/*********************************************************************
*
*       rtt terminal i/o
*
**********************************************************************
*/

/*
 If platform.h defines CUSTOM_RTT_IF the app defines its own rtt input and output.
 If platform.h defines RTT_IN_CDC1 then rtt input and output is using usb cdc1.
 With no CUSTOM_RTT_IF and no RTT_IN_CDC1, 
 the default is a minimal rtt, with output in the gdb session, and no input.
 */

#include "general.h"
#include "platform.h"

#ifndef CUSTOM_RTT_IF

#ifdef RTT_IN_CDC1

/* rtt input from and output to usb cdc1 */

#include "rtt.h"
#include "rtt_if.h"
#include "usb_desc.h"
#include "usb_cdc.h"

/* rtt host to target: read one character */
int32_t rtt_getchar(const uint32_t channel)
{
    if (channel == 0)
        return cdc1_getchar();
    return -1;
}

/* rtt host to target: true if no characters available for reading */
bool rtt_nodata(const uint32_t channel)
{
    if (channel == 0)
        return cdc1_recv_empty();
    return true;
}

/* rtt target to host: write string */
uint32_t rtt_write(const uint32_t channel, const char *buf, uint32_t len)
{
    cdc1_write((uint8_t *)buf, len);
    return len;
}

#else

/* minimal rtt interface: output in gdb, no input. */

#include "rtt.h"
#include "rtt_if.h"
#include "gdb_packet.h"

/* write string to gdb console */
uint32_t gdb_outn(const char *buf, uint32_t len)
{
    const uint32_t max_len = GDB_OUT_PACKET_MAX_SIZE / 2 - 1;
    uint32_t       step;
    while (len > 0)
    {
        step = len > max_len ? max_len : len;
        gdb_put_packet("O", 1U, buf, step, true);
        len -= step;
        buf += step;
    }
    return len;
}

/* rtt host to target: read one character */
int32_t rtt_getchar(const uint32_t channel)
{
    (void)channel;
    return -1;
}

/* rtt host to target: true if no characters available for reading */
bool rtt_nodata(const uint32_t channel)
{
    (void)channel;
    return true;
}

/* rtt target to host: write string */
uint32_t rtt_write(const uint32_t channel, const char *buf, uint32_t len)
{
    (void)channel;
    gdb_outn(buf, len);
    return len;
}

#endif

#endif

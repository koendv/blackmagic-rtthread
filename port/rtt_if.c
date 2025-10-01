/*********************************************************************
*
*       rtt terminal i/o
*
**********************************************************************
*/

#include "general.h"
#include "platform.h"

#ifndef BLACKMAGIC_CUSTOM_RTT_IF

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

#endif

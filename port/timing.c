#include "general.h"
#include "platform.h"

#ifndef BLACKMAGIC_CUSTOM_TIMING

/* delay ms milliseconds */
void platform_delay(uint32_t ms)
{
	if (ms <= 2) {
		uint32_t ticks = SYSTEM_CORE_CLOCK / 1000 * ms;
		uint32_t start = DWT->CYCCNT;
		while ((DWT->CYCCNT - start) < ticks)
			;
	} else
		rt_thread_mdelay(ms);
}

/* milliseconds since boot */
uint32_t platform_time_ms(void)
{
	return rt_tick_get_millisecond();
}

#endif

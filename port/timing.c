#include "general.h"
#include "platform.h"

#ifndef BLACKMAGIC_CUSTOM_TIMING

uint32_t target_clk_divider = 1;

/* delay ms milliseconds */
void platform_delay(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

/* milliseconds since boot */
uint32_t platform_time_ms(void)
{
    return rt_tick_get_millisecond();
}

/* set bit-banging delay */
uint32_t platform_max_frequency_get(void)
{
    uint32_t f;
    if (target_clk_divider == UINT32_MAX)
        f = BLACKMAGIC_FASTCLOCK;
    else
        f = BLACKMAGIC_DELAY_CONSTANT * 1000 / (target_clk_divider + 1);
    return f;
}

void platform_max_frequency_set(const uint32_t frequency)
{
    if (frequency >= BLACKMAGIC_FASTCLOCK)
        target_clk_divider = UINT32_MAX;
    else
        target_clk_divider = BLACKMAGIC_DELAY_CONSTANT * 1000 / (frequency + 1);
}

/*
   FINSH clock_test command 
   toggle SWCLK to calibrate delay loop and determine DELAY_CONSTANT, FASTCLOCK
 */

static void clock_test(int argc, char **argv)
{
    uint32_t delay = 0;
    if (argc >= 2)
        delay = atoi(argv[1]);

    rt_kprintf("delay loops: %d\r\n", delay);

    rt_pin_mode(SWCLK_PIN, PIN_MODE_OUTPUT);

    if (delay)
    {
        while (1)
        {
            rt_pin_write(SWCLK_PIN, PIN_LOW);
            for (volatile uint32_t counter = delay; counter > 0; --counter)
                continue;
            rt_pin_write(SWCLK_PIN, PIN_HIGH);
            for (volatile uint32_t counter = delay; counter > 0; --counter)
                continue;
        }
    }
    else
    {
        while (1)
        {
            rt_pin_write(SWCLK_PIN, PIN_LOW);
            rt_pin_write(SWCLK_PIN, PIN_HIGH);
        }
    }
}

MSH_CMD_EXPORT(clock_test, clock_test NUMBER : calibrate bit - banging delay loop);

#endif

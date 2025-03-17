#include "general.h"
#include "platform.h"
#include <rtthread.h>

#include "usb_cdc.h"

bool running_status = false;

void platform_init(void)
{
#ifdef LED_IDLE_RUN
    rt_pin_write(LED_IDLE_RUN, PIN_HIGH);
    rt_pin_mode(LED_IDLE_RUN, PIN_MODE_OUTPUT);
#endif

#ifdef NRST_OUT_PIN
    rt_pin_write(NRST_OUT_PIN, PIN_LOW);
    rt_pin_mode(NRST_OUT_PIN, PIN_MODE_OUTPUT);
#endif

#ifdef NRST_IN
    rt_pin_mode(NRST_IN_PIN, PIN_MODE_INPUT);
#endif

    rt_pin_write(SWCLK_PIN, PIN_HIGH);
    rt_pin_write(SWDIO_PIN, PIN_HIGH);

    rt_pin_mode(SWCLK_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(SWDIO_PIN, PIN_MODE_OUTPUT);

#ifdef SWCLK_DIR_PIN
    rt_pin_write(SWCLK_DIR_PIN, PIN_HIGH);
    rt_pin_mode(SWCLK_DIR_PIN, PIN_MODE_OUTPUT);
#endif

#ifdef SWDIO_DIR_PIN
    rt_pin_write(SWDIO_DIR_PIN, PIN_HIGH);
    rt_pin_mode(SWDIO_DIR_PIN, PIN_MODE_OUTPUT);
#endif

#ifdef SWO_PIN
    rt_pin_mode(SWO_PIN, PIN_MODE_INPUT);
#endif

#ifdef TDO_PIN
    rt_pin_write(TDO_PIN, PIN_HIGH);
    rt_pin_mode(TDO_PIN, PIN_MODE_OUTPUT);
#endif

#ifdef TARGET_OE_PIN
    rt_pin_mode(TARGET_OE_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(TARGET_OE_PIN, PIN_LOW); // enable SWD/JTAG output
#endif
}

void platform_nrst_set_val(bool assert)
{
#ifdef NRST_OUT_PIN
    if (assert)
    {
        rt_pin_write(NRST_OUT_PIN, PIN_HIGH);
    }
    else
    {
        rt_pin_write(NRST_OUT_PIN, PIN_LOW);
    }
#else
    (void)assert;
#endif
}

bool platform_nrst_get_val(void)
{
#ifdef NRST_IN_PIN
    return (rt_pin_read(NRST_IN_PIN) ? false : true);
#else
    return 1;
#endif
}

/* returns target voltage as a string, e.g. "3.333V" */
const char *platform_target_voltage(void)
{
    return "";
}

int platform_hwversion(void)
{
    return 0;
}

void platform_target_clk_output_enable(bool enable)
{
#ifdef TARGET_SWCLK_DIR_PIN
    if (enable)
        rt_pin_write(TARGET_SWCLK_DIR_PIN, PIN_HIGH); // SWCLK out
    else
        rt_pin_write(TARGET_SWCLK_DIR_PIN, PIN_LOW);  // SWCLK in
#else
    (void)enable;
#endif
}

bool platform_spi_init(const spi_bus_e bus)
{
    (void)bus;
    return false;
}

bool platform_spi_deinit(const spi_bus_e bus)
{
    (void)bus;
    return false;
}

bool platform_spi_chip_select(const uint8_t device_select)
{
    (void)device_select;
    return false;
}

uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
{
    (void)bus;
    return value;
}

void debug_serial_send_stdout(const uint8_t * const data, const size_t len)
{
    cdc1_write((uint8_t *)data, len);
}

size_t debug_serial_debug_write(const char *buf, const size_t len)
{
    cdc1_write((uint8_t *)buf, len);
    return len;
}

void vtarget(int argc, char **argv)
{
    rt_kprintf("vio: %s\r\n", platform_target_voltage());
}

MSH_CMD_EXPORT(vtarget, target voltage);

#ifndef RT_BLACKMAGIC_PLATFORM_H
#define RT_BLACKMAGIC_PLATFORM_H

#include <sys/times.h>
#include <time.h>
#include <strings.h>
#include <stdio.h>

#include <rtthread.h>
#include <rtdevice.h>
#include <drv_gpio.h>

extern bool running_status;
extern bool rtt_input_enable;

#define PLATFORM_IDENT "rt-thread "

/*
 The SWD and JTAG frequency is controlled by a delay loop.
 The relation between frequency and number of delay loop iterations is determined by BLACKMAGIC_DELAY_CONSTANT and BLACKMAGIC_FASTCLOCK.

 BLACKMAGIC_DELAY_CONSTANT is the number of delay loop iterations at which SWCLK frequency is 1 kHz.
 BLACKMAGIC_DELAY_CONSTANT can be determined by executing the "clock_test NUMBER" shell command with varying parameter value and measuring the frequency on the SWCLK pin.

 BLACKMAGIC_FASTCLOCK is the frequency in Hz produced on the SWCLK pin when running the "clock_test 1" shell command.
 For a given frequency, the number of iterations is obtained by interpolation between (1, BLACKMAGIC_FASTCLOCK) and (BLACKMAGIC_DELAY_CONSTANT, 1000).
 If the frequency is higher than BLACKMAGIC_FASTCLOCK, the number of loops is 0.

 You can also measure maximum achievable frequency by running the "clock_test 0" shell command.

 On at32f435:
 clock_test 0 produces 1.02 MHz
 clock_test 1 produces 910 kHz
 clock_test 14300 produces 1 kHz

 These are the speeds when using rt_pin_write().
 Higher speeds are possible writing to registers directly.
 */

#define BLACKMAGIC_DELAY_CONSTANT 14300
#define BLACKMAGIC_FASTCLOCK      910000

#if 0
#define AUX_UART          "uart3"
#define AUX_DEFAULT_SPEED 115200U
#endif

#if 0
#define PLATFORM_HAS_TRACESWO
#define SWO_UART          "uart2"
#define SWO_ENCODING 2
#endif

/* pins. 
   Two considerations:
   - pins in same bank to allow fast SWD/JTAG writes
   - easy pcb layout from pins to a level translator SN74AXC4T774PWR
 */

#define LED_IDLE_RUN         GET_PIN(D, 13)
#define TARGET_SWCLK_DIR_PIN GET_PIN(A, 2)
#define TARGET_SWDIO_DIR_PIN GET_PIN(A, 3)
#define TARGET_SWCLK_PIN     GET_PIN(A, 4)
#define TARGET_SWDIO_PIN     GET_PIN(A, 5)
#define TARGET_TDI_PIN       GET_PIN(A, 6)
#define TARGET_SWO_PIN       GET_PIN(A, 7)
#define TARGET_RST_IN_PIN    GET_PIN(B, 10)
#define TARGET_RST_PIN       GET_PIN(B, 13)

/* target reset pin */
#define NRST_IN_PIN  TARGET_RST_IN_PIN
#define NRST_OUT_PIN TARGET_RST_PIN

/* target swd pins */
#define SWCLK_PIN TARGET_SWCLK_PIN
#define SWDIO_PIN TARGET_SWDIO_PIN
#define SWO_PIN   TARGET_SWO_PIN

/* direction for the level shifters */
#define SWCLK_DIR_PIN TARGET_SWCLK_DIR_PIN
#define SWDIO_DIR_PIN TARGET_SWDIO_DIR_PIN

/* jtag port */
#define TCK_PIN     TARGET_SWCLK_PIN
#define TMS_PIN     TARGET_SWDIO_PIN
#define TDI_PIN     TARGET_TDI_PIN
#define TDO_PIN     TARGET_SWO_PIN
#define TCK_DIR_PIN TARGET_SWCLK_DIR_PIN
#define TMS_DIR_PIN TARGET_SWDIO_DIR_PIN

/* dummy port definitions */
#define NRST_PORT  0
#define SWDIO_PORT 0
#define SWCLK_PORT 0
#define TCK_PORT   0
#define TMS_PORT   0
#define TDI_PORT   0
#define TDO_PORT   0

/* switch pins from read to write */
#define TMS_SET_MODE()                         \
    {                                          \
        rt_pin_mode(TMS_PIN, PIN_MODE_OUTPUT); \
        rt_pin_write(TMS_DIR_PIN, PIN_HIGH);   \
    }
#define SWDIO_MODE_FLOAT()                      \
    {                                           \
        rt_pin_mode(SWDIO_PIN, PIN_MODE_INPUT); \
        rt_pin_write(TMS_DIR_PIN, PIN_LOW);     \
    }
#define SWDIO_MODE_DRIVE()                       \
    {                                            \
        rt_pin_mode(SWDIO_PIN, PIN_MODE_OUTPUT); \
        rt_pin_write(TMS_DIR_PIN, PIN_HIGH);     \
    }

/* activity led */
#ifdef LED_IDLE_RUN
#define SET_RUN_STATE(state)                                    \
    {                                                           \
        rt_pin_write(LED_IDLE_RUN, state ? PIN_LOW : PIN_HIGH); \
        running_status = (state);                               \
    }
#define SET_IDLE_STATE(state)                                   \
    {                                                           \
        rt_pin_write(LED_IDLE_RUN, state ? PIN_HIGH : PIN_LOW); \
    }
#else
#define SET_RUN_STATE(state)      \
    {                             \
        running_status = (state); \
    }
#define SET_IDLE_STATE(state) \
    {                         \
    }
#endif
#define SET_ERROR_STATE(state)

/* libopencm3 to rt-thread */
#define gpio_clear(port, pin)          rt_pin_write(pin, PIN_LOW)
#define gpio_set(port, pin)            rt_pin_write(pin, PIN_HIGH);
#define gpio_set_val(port, pin, value) rt_pin_write(pin, value ? PIN_HIGH : PIN_LOW)
#define gpio_get(port, pin)            rt_pin_read(pin)
#define strnlen(s, maxlen)             rt_strnlen((s), (maxlen))

#endif

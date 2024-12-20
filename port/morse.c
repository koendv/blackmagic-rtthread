/*
 * legacy code.
 * the original hardware does not have a display.
 * instead, error messages are displayed by blinking a led in morse code.
 * this code added for compatibility with the original hardware.
 */

#include "general.h"
#include "morse.h"

volatile const char *morse_msg = NULL;

void morse(const char * const msg, const bool repeat)
{
    (void)repeat;
    morse_msg = msg;
}

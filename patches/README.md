# PATCHES

These are patches to black magic debug

- **port-rtthread.patch**
rt-thread port

- **memwatch.patch**
software watchpoints. adds "mon memwatch" gdb command. See docs/UsingMemwatch.md

- **rtt_halt.patch**
adds "mon rtt halt [enable|disable|auto]" gdb command.
allows user to force halting/not halting target processor within rtt. 


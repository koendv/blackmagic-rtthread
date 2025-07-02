# PATCHES

These are patches to black magic debug

- **01-port-rtthread.patch**
rt-thread port

- **02-memwatch.patch**
software watchpoints. adds "mon memwatch" gdb command. See docs/UsingMemwatch.md

- **03-rtt-halt.patch**
adds "mon rtt halt [enable|disable|auto]" gdb command.
allows user to force rtt to halt or not to halt target processor. 

code:
```
cd packages/blackmagic-latest
patch -p1 < patches/01-port-rtthread.patch
patch -p1 < patches/02-memwatch.patch
patch -p1 < patches/03-rtt-halt.patch
```

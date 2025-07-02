On some processors rtt has to briefly halt the target processor before accessing target memory.
The default is not halting target processor.
On risc-v and TM4C123x, the default is halting the target when accessing rtt.
In case the default choice is insatisfactory, force behavior with the _mon rtt halt_ gdb command.

```
$ arm-none-eabi-gdb
(gdb) tar ext /dev/ttyACM0
(gdb) mon swd
(gdb) at 1
Attaching to Remote target
```
Enable rtt. Note rtt will not halt the target processor. (halt is off)
```
(gdb) mon rtt
(gdb) mon rtt status
rtt: on found: no ident: off halt: off channels: auto
max poll ms: 256 min poll ms: 8 max errs: 10
```
Force rtt to halt the target processor.
```
(gdb) mon rtt halt enable
(gdb) mon rtt status
rtt: on found: no ident: off halt: on channels: auto
max poll ms: 256 min poll ms: 8 max errs: 10
```
Force rtt not to halt the target processor.
```
(gdb) mon rtt halt disable
(gdb) mon rtt status
rtt: on found: no ident: off halt: off channels: auto
max poll ms: 256 min poll ms: 8 max errs: 10
```
Back to default.
```
(gdb) mon rtt halt auto
```

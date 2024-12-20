# blackmagic-rtthread

This is the blackmagic debugger running on the rt-thread operating system.

[blackmagic](https://black-magic.org/) is an open source JTAG/SWD debugger for embedded microcontrollers.
[RT-Thread](https://www.rt-thread.io/) is an open source IoT real-time operating system (RTOS).

## description

The "black magic" rt-thread package implements a gdb server on a usb serial port. The rt-thread package has been tested on full speed and high speed usb with [CherryUSB](https://github.com/cherry-embedded/CherryUSB):

- [Artery (雅特力) at32f435](https://github.com/koendv/at32f435-start) full-speed usb
- [Artery (雅特力) at32f405](https://github.com/koendv/at32f405-app) high-speed usb

More information about the gdb server can be found on the [web site](https://black-magic.org/) and in the [handbook](https://github.com/compuphase/Black-Magic-Probe-Book/releases/latest/download/BlackMagicProbe.pdf).

## installation

Add the blackmagic-rtthread git to the rt-thread packages by hand:

```
$ cd .env/packages/packages/
$ wget https://raw.githubusercontent.com/koendv/blackmagic-rtthread/refs/heads/main/patches/package.patch
$ patch -p1 < package.patch 
patching file tools/Kconfig
patching file tools/blackmagic/Kconfig
patching file tools/blackmagic/package.json
```

In the project directory run `menuconfig`. 
```
RT-Thread online packages  --->
    tools packages  --->
[*] black magic probe: firmware download tool  --->
```
then
```
$ pkgs --update
```
This downloads the blackmagic-rtthread package to your project.

## links

- [at32f435 sources](https://github.com/koendv/at32f435-start)
- [at32f405 sources](https://github.com/koendv/at32f405-app)
- [at32f405 hardware project](https://oshwlab.com/koendv/at32f405-tool)
- [at32f435 hardware project](https://oshwlab.com/koendv/at32f435-board)
- [blackmagic sources](https://github.com/blackmagic-debug/blackmagic)
- [rt-thread sources](https://github.com/RT-Thread/rt-thread)


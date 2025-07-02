# blackmagic-rtthread

[black magic debug](https://black-magic.org/) is a open source debug probe, used to download firmware to arm and risc-v processors. This is *black magic debug* running on the [rt-thread](https://www.rt-thread.io/) operating system.

## description

The _black magic debug_ rt-thread package implements a gdb server on a usb serial port. The rt-thread package has been tested on full speed and high speed usb with [CherryUSB](https://github.com/cherry-embedded/CherryUSB):

- [Artery at32f435](https://github.com/koendv/at32f435-start) full-speed usb
- [Artery at32f405](https://github.com/koendv/at32f405-app) high-speed usb

More information about _black magic debug_ can be found on the [web site](https://black-magic.org/) and in the [handbook](https://github.com/compuphase/Black-Magic-Probe-Book/releases/latest/download/BlackMagicProbe.pdf).

## adding

To add _black magic debug_ to your rt-thread project, in the project directory run `menuconfig`.

```
RT-Thread online packages  --->
    tools packages  --->
[*] black magic debug: firmware download tool  --->
```

then

```
$ pkgs --update
```

This downloads the blackmagic-rtthread package to your project.

## configuration

*black magic debug* needs two configuration files: `platform.h, platform.c`

Sample configuration files are in the `example/` directory. Copy to your project directory and edit.

## swo

Serial Wire Output (SWO) uses a single processor pin for printf output. Capturing the SWO data is outside this project. To implement SWO, set up a high-speed UART with dual-bank RX DMA and feed the received characters to *black magic debug* swo_itm_decode(). In platform.h, set PLATFORM_HAS_TRACESWO. 

## links

- [at32f435 sources](https://github.com/koendv/at32f435-start) full-speed usb
- [at32f405 sources](https://github.com/koendv/at32f405-app) high-speed usb
- [black magic debug](https://github.com/blackmagic-debug/blackmagic) upstream


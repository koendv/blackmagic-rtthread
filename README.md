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

Serial Wire Output (SWO) uses a single processor pin for printf output. Capturing the SWO data is outside this project. To implement SWO, set up a high-speed UART with dual-bank RX DMA and feed the received characters to *black magic debug* swo_itm_decode(). In platform.h, set PLATFORM_HAS_TRACESWO. The patches/ directory contains a patch to compile swo_itm_decode() on rt-thread.

## updating black magic debug source

These are notes how to update the git repository to the latest upstream _black magic debug_ version.

First replace the `blackmagic-rtthread/blackmagic/src` directory with the new version.

```
git clone https://github.com/blackmagic-debug/blackmagic
git clone https://github.com/koendv/blackmagic-rtthread
rm -rf blackmagic-rtthread/blackmagic/src/
mv blackmagic/src/ blackmagic-rtthread/blackmagic
cd blackmagic-rtthread/blackmagic/
patch -p1 < ../patches/bmp-rtthread.patch
patch -p1 < ../patches/memwatch.patch
git status
```

_git add -u_ will add all files that _git status_ lists as "modified".

```
git add -u
git status
```

Check `src/target` for source files for new targets. Add omissions with *git add*.

Do not include src/target/jtagtap_generic.c and src/target/swdptap_generic.c, use src/platforms/common/jtagtap.c and src/platforms/common/swdptap.c instead.

Then push the update:

```
git commit -m update
git push
```

## links

- [at32f435 sources](https://github.com/koendv/at32f435-start) full-speed usb
- [at32f405 sources](https://github.com/koendv/at32f405-app) high-speed usb
- [black magic debug](https://github.com/blackmagic-debug/blackmagic) upstream

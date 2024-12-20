# blackmagic-rtthread

This is [black magic debug](https://black-magic.org/) running on the [rt-thread](https://www.rt-thread.io/) operating system. _black magic debug_ is a open source debug probe, used to download firmware to arm and risc-v processors.

## description

The _black magic debug_ rt-thread package implements a gdb server on a usb serial port. The rt-thread package has been tested on full speed and high speed usb with [CherryUSB](https://github.com/cherry-embedded/CherryUSB):

- [Artery (雅特力) at32f435](https://github.com/koendv/at32f435-start) full-speed usb
- [Artery (雅特力) at32f405](https://github.com/koendv/at32f405-app) high-speed usb

More information about _black magic debug_ can be found on the [web site](https://black-magic.org/) and in the [handbook](https://github.com/compuphase/Black-Magic-Probe-Book/releases/latest/download/BlackMagicProbe.pdf).

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
[*] black magic debug: firmware download tool  --->
```

then

```
$ pkgs --update
```

This downloads the blackmagic-rtthread package to your project.

## configuration

Two configuration files are needed: `platform.h, platform.c`

Sample configuration files are in the `example/` directory. Copy to your project directory and edit.

## updating black magic debug source

A description how to update the git repository to the latest upstream _black magic debug_ version.

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

Check `src/target` for source files for new targets. Add new source files with *git add*.

Then push the update:

```
git commit -m update
git push
```

## links

- [at32f435 sources](https://github.com/koendv/at32f435-start)
- [at32f405 sources](https://github.com/koendv/at32f405-app)
- [at32f435 hardware project](https://oshwlab.com/koendv/at32f435-board)
- [at32f405 hardware project](https://oshwlab.com/koendv/at32f405-tool)
- [black magic debug sources](https://github.com/blackmagic-debug/blackmagic)
- [rt-thread sources](https://github.com/RT-Thread/rt-thread)

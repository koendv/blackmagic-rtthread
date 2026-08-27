blackmagic-rtthread
====================

`Black Magic Debug <https://black-magic.org/>`_ running on `RT-Thread
<https://www.rt-thread.io/>`_ - a gdb server on a usb serial port, for downloading
firmware to arm and risc-v processors. Tested with `CherryUSB
<https://github.com/cherry-embedded/CherryUSB>`_.

See the `handbook
<https://github.com/compuphase/Black-Magic-Probe-Book/releases/latest/download/BlackMagicProbe.pdf>`_
for details on *Black Magic Debug* itself.

Adding
------

::

    RT-Thread online packages  --->
        tools packages  --->
    [*] black magic debug: firmware download tool  --->

    $ pkgs --update

Configuration
-------------

Needs ``platform.h`` and ``platform.c``. See arm_can_tool_ for a working reference.

SWO
---

Feed a high-speed UART with dual-bank RX DMA into ``swo_itm_decode()``, and set
``PLATFORM_HAS_TRACESWO`` in ``platform.h``.

Links
-----

- arm_can_tool_
- `Black Magic Debug upstream <https://github.com/blackmagic-debug/blackmagic>`_

.. _arm_can_tool: https://github.com/koendv/arm_can_tool

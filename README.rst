blackmagic-rtthread
====================

`Black Magic Debug <https://black-magic.org/>`_ running on `RT-Thread
<https://www.rt-thread.io/>`_ - an embedded gdb server on a usb serial port, for
downloading firmware to arm and risc-v processors.

- SWD and JTAG debug probe
- RTT, DWT trace over SWO, and MTB support
- Live memory watchpoints while target runs
- USB CDC transport via `CherryUSB <https://github.com/cherry-embedded/CherryUSB>`_

See arm_can_tool_ for reference implementation.

See the `handbook
<https://github.com/compuphase/Black-Magic-Probe-Book/releases/latest/download/BlackMagicProbe.pdf>`_
for details on *Black Magic Debug* itself.

Links
-----

- arm_can_tool_
- `Black Magic Debug upstream <https://codeberg.org/blackmagic-debug/blackmagic>`_

.. _arm_can_tool: https://github.com/koendv/arm_can_tool

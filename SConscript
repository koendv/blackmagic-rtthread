Import('RTT_ROOT')
from building import *
from os.path import join

LOCAL_CPPDEFINES = ['CONFIG_BMDA=0', 'NO_LIBOPENCM3']
LOCAL_CPPDEFINES += ['ENABLE_MEMWATCH=1']

LOCAL_CPPDEFINES += [ 'CONFIG_CORTEXM=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_CORTEXAR=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_AT32=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_APOLLO3=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_CH32=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_CH579=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_EFM32=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_GD32=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_HC32=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_LPC=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_MM32=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_NRF=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_NXP=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_PUYA=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_RA=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_RZ=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_RP=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_SAM=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_STM=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_TI=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_XILINX=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_RISCV=1' ]
LOCAL_CPPDEFINES += [ 'CONFIG_RISCV_ACCEL=1' ]
LOCAL_CPPDEFINES += [ 'SAMX5X_EXTRA_CMDS=1' ]

# get current directory
cwd = GetCurrentDir()

# The set of source files associated with this SConscript file.

src = Glob(join('src', '*.c'), exclude=[join('src', 'main.c'), join('src', 'rtt.c'), join('src', 'morse.c')])
src += Glob(join('src', 'target', '*.c'), exclude=[join('src', 'target', 'swdptap_generic.c'), join('src', 'target', 'jtagtap_generic.c')])
src += Glob(join('src', 'platforms', 'common', 'stm32', 'swo_itm_decode.c'))
src += Glob(join('port', '*.c'), exclude=[join('port', 'rtt_if.c')])

# gpio
if GetDepend(['BLACKMAGIC_ENABLE_GPIO']):
    src += Glob(join('src', 'platforms', 'common', 'jtagtap.c'))
    src += Glob(join('src', 'platforms', 'common', 'swdptap.c'))

# rtt
if GetDepend(['BLACKMAGIC_ENABLE_RTT']):
    src += [join('src', 'rtt.c')]
    src += [join('port', 'rtt_if.c')]
    LOCAL_CPPDEFINES += ['ENABLE_RTT=1']

if GetDepend(['BLACKMAGIC_ENABLE_RTT_IDENT']):
    LOCAL_CPPDEFINES += ['RTT_IDENT=${BLACKMAGIC_RTT_IDENT}']

path = [join(cwd, 'port')]
path += [join(cwd, 'src')]
path += [join(cwd, 'src', 'include')]
path += [join(cwd, 'src', 'platforms', 'common')]
path += [join(cwd, 'src', 'target')]

group = DefineGroup('blackmagic', src, depend = ['PKG_USING_BLACKMAGIC'], CPPPATH = path, CPPDEFINES = LOCAL_CPPDEFINES)

Return('group')

Import('RTT_ROOT')
from building import *
from os.path import join

LOCAL_CCFLAGS = '-DCONFIG_BMDA=0 -DNO_LIBOPENCM3'
LOCAL_CCFLAGS += ' -DCONFIG_CORTEXAR=1 -DCONFIG_GD32 -DCONFIG_MM32 -DCONFIG_XILINX=1'
LOCAL_CCFLAGS += ' -DCONFIG_RISCV=1 -DCONFIG_RISCV_ACCEL=1'
LOCAL_CCFLAGS += ' -DENABLE_MEMWATCH=1'

# get current directory
cwd = GetCurrentDir()

# The set of source files associated with this SConscript file.

src = Glob(join('blackmagic', 'src', '*.c'), exclude=[join('blackmagic', 'src', 'rtt.c')])
src += Glob(join('blackmagic', 'src', 'platforms', 'common', '*.c'))
src += Glob(join('blackmagic', 'src', 'target', '*.c'))
src += Glob(join('port', '*.c'), exclude=[join('port', 'rtt_if.c')])

# rtt
if GetDepend(['BLACKMAGIC_ENABLE_RTT']):
    src += [join('blackmagic', 'src', 'rtt.c')]
    src += [join('port', 'rtt_if.c')]
    LOCAL_CCFLAGS += ' -DENABLE_RTT=1'

if GetDepend(['BLACKMAGIC_ENABLE_RTT_IDENT']):
    LOCAL_CCFLAGS += ' -DRTT_IDENT=${BLACKMAGIC_RTT_IDENT}'

path = [join(cwd, 'port')]
path += [join(cwd, 'blackmagic', 'src')]
path += [join(cwd, 'blackmagic', 'src', 'include')]
path += [join(cwd, 'blackmagic', 'src', 'platforms', 'common')]
path += [join(cwd, 'blackmagic', 'src', 'target')]

group = DefineGroup('blackmagic', src, depend = ['PKG_USING_BLACKMAGIC'], CPPPATH = path, LOCAL_CCFLAGS = LOCAL_CCFLAGS)

Return('group')

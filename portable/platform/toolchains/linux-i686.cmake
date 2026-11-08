# Little-endian, 32-bit Linux target; QEMU makes execution host-independent.
set(CMAKE_SYSTEM_PROCESSOR i686)
set(MKSC_CROSS_TRIPLE i686-linux-gnu)
set(MKSC_QEMU_NAME qemu-i386)
include("${CMAKE_CURRENT_LIST_DIR}/linux-cross.cmake")

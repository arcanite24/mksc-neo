# Big-endian, 32-bit Linux target; use the same core, application and tests.
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(MKSC_CROSS_TRIPLE powerpc-linux-gnu)
set(MKSC_QEMU_NAME qemu-ppc)
include("${CMAKE_CURRENT_LIST_DIR}/linux-cross.cmake")

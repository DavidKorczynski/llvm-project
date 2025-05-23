add_entrypoint_object(
  madvise
  SRCS
    madvise.cpp
  HDRS
    ../madvise.h
  DEPENDS
    libc.include.sys_mman
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  mmap
  SRCS
    mmap.cpp
  HDRS
    ../mmap.h
  DEPENDS
    libc.include.sys_mman
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
  COMPILE_OPTIONS
    # TODO: https://github.com/llvm/llvm-project/issues/77395
    -Wno-shorten-64-to-32
)

add_entrypoint_object(
  munmap
  SRCS
    munmap.cpp
  HDRS
    ../munmap.h
  DEPENDS
    libc.include.sys_mman
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  mprotect
  SRCS
    mprotect.cpp
  HDRS
    ../mprotect.h
  DEPENDS
    libc.include.sys_mman
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  posix_madvise
  SRCS
    posix_madvise.cpp
  HDRS
    ../posix_madvise.h
  DEPENDS
    libc.include.sys_mman
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
)

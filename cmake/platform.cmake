# --------------------------------------------------------------------------------------------------
# CMake platform utilities.
# --------------------------------------------------------------------------------------------------

#
# Detect the current platform
# Sets 'VOLO_PLATFORM' to either:
# * linux
# * win32
#
macro(detect_platform)
  if(UNIX AND NOT APPLE)
    message(STATUS "Detected linux platform")
    set(VOLO_PLATFORM "linux")
  elseif(WIN32)
    message(STATUS "Detected win32 platform")
    set(VOLO_PLATFORM "win32")
  else()
    message(FATAL_ERROR "Unsupported platform")
  endif()
endmacro(detect_platform)

#
# Set linux specific defines
#
macro(set_linux_defines)
  message(STATUS "Configuring linux platform defines")

  add_definitions(-DVOLO_LINUX)
  add_definitions(-D_GNU_SOURCE) # Enable GNU extensions.
  add_definitions(-DNDEBUG) # Disable lib-c assertions (our own assertions are independent of this).
endmacro(set_linux_defines)

#
# Set windows specific defines
#
macro(set_win32_defines)
  message(STATUS "Configuring win32 platform defines")

  add_definitions(-DVOLO_WIN32)
  add_definitions(-DWINVER=0x0603 -D_WIN32_WINNT=0x0603) # Target windows '8.1'
  add_definitions(-DWIN32_LEAN_AND_MEAN) # Use a subset of the windows header.
  add_definitions(-DNOMINMAX) # Avoid the windows header defining the min / max macros.
  add_definitions(-DUNICODE) # Enable unicode support.
  add_definitions(-DNDEBUG) # Disable lib-c assertions (our own assertions are independent of this).
endmacro(set_win32_defines)

#
# Set platform specific defines
# Requires 'VOLO_PLATFORM' to be configured
#
macro(set_platform_defines)
  if(${VOLO_PLATFORM} STREQUAL "linux")

    set_linux_defines()

    # Enable pthread threading.
    add_compile_options(-pthread)
    add_link_options(-pthread)

  elseif(${VOLO_PLATFORM} STREQUAL "win32")

    set_win32_defines()

  else()
    message(FATAL_ERROR "Unknown platform")
  endif()
endmacro(set_platform_defines)

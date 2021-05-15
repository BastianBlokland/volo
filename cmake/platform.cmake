# --------------------------------------------------------------------------------------------------
# CMake platform utilities.
# --------------------------------------------------------------------------------------------------

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

# Set linux specific defines
macro(set_linux_defines)
  add_definitions(-DVOLO_LINUX)
endmacro(set_linux_defines)

# Set linux windows defines
macro(set_win32_defines)
  add_definitions(-DVOLO_WIN32)
  add_definitions(-DWINVER=0x0602 -D_WIN32_WINNT=0x0602) # Target windows '8'
  add_definitions(-DWIN32_LEAN_AND_MEAN) # Use a subset of the windows header.
  add_definitions(-DNOMINMAX) # Avoid the windows header defining the min / max macros.
endmacro(set_win32_defines)

# Set platform specific defines
# Requires 'VOLO_PLATFORM' to be configured
macro(set_platform_defines)
  if(${VOLO_PLATFORM} STREQUAL "linux")
    set_linux_defines()
  elseif(${VOLO_PLATFORM} STREQUAL "win32")
    set_win32_defines()
  else()
    message(FATAL_ERROR "Unknown platform")
  endif()
endmacro(set_platform_defines)

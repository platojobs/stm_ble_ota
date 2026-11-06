# CMake toolchain for STM32C5 (Cortex-M33, single-precision FPU).
#
# The Homebrew "arm-none-eabi-gcc" formula ships the compiler but not newlib,
# so it cannot link a bare-metal C program that uses stdint.h/snprintf. This
# toolchain file therefore prefers the official Arm GNU Toolchain, which
# bundles newlib:
#
#   1. install the official package (see README "Toolchain"), or
#   2. drop an extracted toolchain under ~/tools/arm-gnu-toolchain-*/ ,
#   3. set ARM_TOOLCHAIN_BIN to its bin directory.
#
# A plain PATH lookup is used only as a last resort, with a warning.

if(DEFINED ENV{ARM_TOOLCHAIN_BIN})
    set(_arm_toolchain_candidates "$ENV{ARM_TOOLCHAIN_BIN}")
else()
    file(GLOB _arm_toolchain_candidates
        "$ENV{HOME}/tools/arm-gnu-toolchain-*/bin"
        "/Applications/ArmGNUToolchain/*/bin"
        "/Applications/ARM/bin"
    )
endif()

set(ARM_TOOLCHAIN_BIN "")
foreach(_candidate ${_arm_toolchain_candidates})
    if(EXISTS "${_candidate}/arm-none-eabi-gcc"
       AND EXISTS "${_candidate}/../arm-none-eabi/include/stdint.h")
        set(ARM_TOOLCHAIN_BIN "${_candidate}")
        break()
    endif()
endforeach()

if(NOT ARM_TOOLCHAIN_BIN)
    find_program(_arm_gcc_fallback arm-none-eabi-gcc)
    if(_arm_gcc_fallback)
        get_filename_component(ARM_TOOLCHAIN_BIN "${_arm_gcc_fallback}" DIRECTORY)
        message(WARNING
            "Using ${ARM_TOOLCHAIN_BIN} from PATH. If the link fails with "
            "missing libc/newlib, install the official Arm GNU Toolchain and "
            "point ARM_TOOLCHAIN_BIN at its bin directory.")
    else()
        message(FATAL_ERROR "arm-none-eabi-gcc not found; install the Arm GNU Toolchain")
    endif()
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   "${ARM_TOOLCHAIN_BIN}/arm-none-eabi-gcc")
set(CMAKE_ASM_COMPILER "${ARM_TOOLCHAIN_BIN}/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER "${ARM_TOOLCHAIN_BIN}/arm-none-eabi-g++")
set(CMAKE_OBJCOPY      "${ARM_TOOLCHAIN_BIN}/arm-none-eabi-objcopy")
set(CMAKE_SIZE         "${ARM_TOOLCHAIN_BIN}/arm-none-eabi-size")

# Prevent CMake from trying to link a host executable during compiler checks.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -ffunction-sections -fdata-sections")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -Wl,--gc-sections -Wl,--no-warn-rwx-segments")

# Keep the build reproducible and quiet about the toolchain location.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# cmake -DMSVC_ROOT=<toolset_dir> -DSDK_ROOT=<sdk_dir> ...
#
#   msvc-wine : MSVC_ROOT = vc/tools/msvc/<ver>
#               SDK_ROOT  = kits/10
#   VS install: MSVC_ROOT = VC/Tools/MSVC/<ver>
#               SDK_ROOT  = Windows Kits/10
#   xwin      : MSVC_ROOT = crt
#               SDK_ROOT  = sdk

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)

set(MSVC_ROOT "" CACHE PATH "MSVC toolset dir containing include/ and lib/<arch>")
set(SDK_ROOT "" CACHE PATH "Windows SDK dir containing include/ and lib/" )

function(_msvc_dir out dir)
    if(NOT IS_DIRECTORY "${dir}")
        return()
    endif()

    list(APPEND "${out}" "${dir}")
    set("${out}" "${${out}}" PARENT_SCOPE)
endfunction()

function(_msvc_base out base)
    if(NOT IS_DIRECTORY "${base}")
        return()
    endif()

    foreach(_flat IN ITEMS um ucrt)
        if(NOT IS_DIRECTORY "${base}/${_flat}")
            continue()
        endif()

        set("${out}" "${base}" PARENT_SCOPE)
        return()
    endforeach()

    file(GLOB _children LIST_DIRECTORIES TRUE "${base}/*")
    list(SORT _children)
    list(POP_BACK _children _newest)
    if(IS_DIRECTORY "${_newest}")
        set("${out}" "${_newest}" PARENT_SCOPE)
    endif()
endfunction()

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MSVC_ROOT SDK_ROOT)
if(NOT MSVC_ROOT OR NOT SDK_ROOT)
    message(FATAL_ERROR "clang-cl toolchain: pass -DMSVC_ROOT=<MSVC toolset dir> and -DSDK_ROOT=<Windows SDK dir>.")
endif()
if(NOT IS_DIRECTORY "${MSVC_ROOT}/include")
    message(FATAL_ERROR "clang-cl toolchain: MSVC_ROOT=${MSVC_ROOT} has no include/ directory.")
endif()

set(_incl)
set(_libs)
list(APPEND _incl "${MSVC_ROOT}/include")
foreach(_arch IN ITEMS x64 x86_64)
    _msvc_dir(_libs "${MSVC_ROOT}/lib/${_arch}")
endforeach()
if(NOT _libs)
    message(FATAL_ERROR "clang-cl toolchain: MSVC_ROOT=${MSVC_ROOT} has no lib/x64 or lib/x86_64 directory.")
endif()
_msvc_dir(_incl "${MSVC_ROOT}/atlmfc/include")
_msvc_dir(_libs "${MSVC_ROOT}/atlmfc/lib/x64")

set(_sdk_inc "")
_msvc_base(_sdk_inc "${SDK_ROOT}/Include")
if(NOT _sdk_inc)
    _msvc_base(_sdk_inc "${SDK_ROOT}/include")
endif()
if(NOT _sdk_inc)
    message(FATAL_ERROR "clang-cl toolchain: SDK_ROOT=${SDK_ROOT} has no Include/ (or include/) directory.")
endif()
foreach(_part IN ITEMS shared ucrt um winrt km cppwinrt)
    _msvc_dir(_incl "${_sdk_inc}/${_part}")
endforeach()

set(_sdk_lib "")
_msvc_base(_sdk_lib "${SDK_ROOT}/Lib")
if(NOT _sdk_lib)
    _msvc_base(_sdk_lib "${SDK_ROOT}/lib")
endif()
if(NOT _sdk_lib)
    message(FATAL_ERROR "clang-cl toolchain: SDK_ROOT=${SDK_ROOT} has no Lib/ (or lib/) directory.")
endif()
foreach(_part IN ITEMS ucrt um km)
    foreach(_arch IN ITEMS x64 x86_64)
        _msvc_dir(_libs "${_sdk_lib}/${_part}/${_arch}")
    endforeach()
endforeach()
foreach(_part IN ITEMS ucrt um)
    _msvc_dir(_libs "${_sdk_lib}/${_part}")
endforeach()

list(JOIN _incl ";" _inc_str)
set(ENV{INCLUDE} "${_inc_str}")
list(JOIN _libs ";" _lib_str)
set(ENV{LIB} "${_lib_str}")

# Include dirs use -imsvc (not -isystem): -imsvc entries search AFTER
# clang's own resource headers, so MSVC's declaration-only intrinsics
# headers (emmintrin.h etc.) never shadow the implementations clang ships.
set(_sysinc_flags "")
foreach(_dir IN LISTS _incl)
    string(APPEND _sysinc_flags " -imsvc\"${_dir}\"")
endforeach()
set(_libpath_flags "")
foreach(_dir IN LISTS _libs)
    string(APPEND _libpath_flags " /LIBPATH:\"${_dir}\"")
endforeach()
set(_rc_inc_flags "")
foreach(_dir IN LISTS _incl)
    string(APPEND _rc_inc_flags " /I \"${_dir}\"")
endforeach()

set(CMAKE_C_FLAGS_INIT "${_sysinc_flags} -Wno-unused-command-line-argument -Wno-unknown-warning-option")
set(CMAKE_CXX_FLAGS_INIT "${_sysinc_flags} -Wno-unused-command-line-argument -Wno-unknown-warning-option")
set(CMAKE_RC_FLAGS_INIT "${_rc_inc_flags}")
foreach(_link_type IN ITEMS EXE SHARED MODULE)
    set(CMAKE_${_link_type}_LINKER_FLAGS_INIT "${_libpath_flags}")
endforeach()

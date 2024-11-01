if(EXISTS /usr/local/include/livox_sdk.h AND EXISTS /usr/local/lib/liblivox_sdk_static.a)
    set(Livox_INCLUDE_DIRS /usr/local/include/)
    set(Livox_LIBS /usr/local/lib/liblivox_sdk_static.a)
    set(Livox_FOUND True)
    message(STATUS "Livox found: ${Livox_LIBS}")
else()
    set(Livox_FOUND False)
    message(FATAL_ERROR "Livox SDK not found")
endif()
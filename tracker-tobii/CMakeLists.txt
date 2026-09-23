# On Windows, Tobii's own Stream Engine SDK:
#   https://www.nuget.org/packages/Tobii.StreamEngine.Native/
# On Linux, libtobii.so, which presents the same C ABI on top of the tobiid
# daemon; point SDK_TOBII at its install prefix, e.g. -DSDK_TOBII=/usr/local.
if(WIN32 OR LINUX)
    set(SDK_TOBII "" CACHE PATH "Tobii Stream Engine path")
endif()
if(SDK_TOBII AND (WIN32 OR LINUX))
    otr_module(tracker-tobii)

    target_include_directories(${self} SYSTEM PRIVATE "${SDK_TOBII}/include")

    if(WIN32)
        if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
            set(arch "x86")
        else()
            set(arch "x64")
        endif()

        target_link_directories(${self} PRIVATE "${SDK_TOBII}/lib/${arch}")

        set(dll "${SDK_TOBII}/lib/${arch}/tobii_stream_engine.dll")
        set(lib tobii_stream_engine.lib)

        target_link_libraries(${self} PRIVATE ${lib})
        install(FILES ${dll} DESTINATION ${opentrack-libexec})
    else()
        # libtobii.so is installed system-wide and pulled in by the loader, so
        # unlike the Windows DLL there is nothing to copy into libexec.
        target_link_directories(${self} PRIVATE "${SDK_TOBII}/lib")
        target_link_libraries(${self} PRIVATE tobii)
    endif()
endif()

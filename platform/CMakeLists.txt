if(WIN32)
	set(BRTOY_PLATFORM_SOURCES platform_win32.cpp)
	set(BRTOY_PLATFORM_DEFINES PUBLIC UNICODE _UNICODE)
endif()

add_library(brtoy_platform platform.cpp ${BRTOY_PLATFORM_SOURCES})
target_compile_definitions(brtoy_platform ${BRTOY_PLATFORM_DEFINES})
target_link_libraries(brtoy_platform PUBLIC brtoy_core)
target_include_directories(brtoy_platform PUBLIC include)
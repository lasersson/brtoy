if(WIN32)
	set(BRTOY_GFX_SOURCES gfx_win32.cpp)
endif()

add_library(brtoy_gfx ${BRTOY_GFX_SOURCES} gfx.cpp gfx_utils.cpp gfx_swapchain.cpp)
target_link_libraries(brtoy_gfx PUBLIC Vulkan::Headers Vulkan::Vulkan brtoy_core VulkanMemoryAllocator)
target_include_directories(brtoy_gfx PUBLIC include)
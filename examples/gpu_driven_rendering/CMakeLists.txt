add_executable(example_gpu_driven_rendering gpu_driven_rendering.cpp)
target_link_libraries(example_gpu_driven_rendering PRIVATE brtoy_platform brtoy_gfx)

target_shader(example_gpu_driven_rendering
	COMPUTE cull_instances
	SOURCE world.hlsl
	ENTRY_POINT cullInstances
)
target_shader(example_gpu_driven_rendering
	VERT world_vs
	SOURCE world.hlsl
	ENTRY_POINT vsMain
)
target_shader(example_gpu_driven_rendering
	FRAG world_fs
	SOURCE world.hlsl
	ENTRY_POINT fsMain
)


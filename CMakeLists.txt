cmake_minimum_required(VERSION 3.18)
project(brtoy VERSION 0.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
if(MSVC)
	add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
	if (MSVC_VERSION GREATER_EQUAL 1914)
		add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
	endif()
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


add_subdirectory(external)

function(target_shader TARGET)
	set(options)
	set(one_value_args VERT FRAG COMPUTE SOURCE ENTRY_POINT)
	set(multi_value_args)
	cmake_parse_arguments(TARGET_SHADER "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
	message(STATUS "VERT: " ${TARGET_SHADER_VERT})
	message(STATUS "FRAG: " ${TARGET_SHADER_FRAG})
	message(STATUS "COMPUTE: " ${TARGET_SHADER_COMPUTE})
	message(STATUS "SOURCE: " ${TARGET_SHADER_SOURCE})
	message(STATUS "ENTRY_POINT: " ${TARGET_SHADER_ENTRY_POINT})

	if (NOT "${TARGET_SHADER_VERT}" STREQUAL "")
		set(stage vert)
		set(profile vs_6_5)
		set(output_filename ${TARGET_SHADER_VERT})
	endif()
	if (NOT "${TARGET_SHADER_FRAG}" STREQUAL "")
		set(stage frag)
		set(profile ps_6_5)
		set(output_filename ${TARGET_SHADER_FRAG})
	endif()
	if (NOT "${TARGET_SHADER_COMPUTE}" STREQUAL "")
		set(stage comp)
		set(profile cs_6_5)
		set(output_filename ${TARGET_SHADER_COMPUTE})
	endif()
	message(STATUS "output_filename: " ${output_filename})
	get_target_property(output_dir ${TARGET} RUNTIME_OUTPUT_DIRECTORY)
	set(output_path ${output_dir}/${output_filename}.spv)
	message(STATUS "output_path: ${output_path}")

	set(shaders_source_dir "${CMAKE_CURRENT_SOURCE_DIR}")
	set(input_path ${shaders_source_dir}/${TARGET_SHADER_SOURCE})

	get_target_property(compiler_bin_dir glslang RUNTIME_OUTPUT_DIRECTORY)
	set(compiler_exe ${compiler_bin_dir}/glslangValidator.exe)
	set(compiler_args -D -V --target-env vulkan1.3 -S ${stage} --invert-y -e ${TARGET_SHADER_ENTRY_POINT} -o ${output_path} ${input_path})
	set(shader_target_deps glslangValidator)
	message(STATUS "compiler_cmd: " ${compiler_exe} ${compiler_args})
	add_custom_command(
		OUTPUT ${output_path}
		COMMAND ${compiler_exe} ARGS ${compiler_args}
		DEPENDS ${input_path}
	)
	set(shader_target ${TARGET}_${output_filename})
	message(STATUS "shader target: ${shader_target}")
	add_custom_target(
		${shader_target}
		DEPENDS ${shader_target_deps} ${output_path}
	)
	add_dependencies(${TARGET} ${shader_target})
endfunction()

add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(gfx)
add_subdirectory(examples)

cmake_minimum_required(VERSION 3.10)

project(RTX C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED COMPONENTS glslc)

if(WIN32)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
endif()

add_subdirectory(thirdparty/glfw)
add_subdirectory(thirdparty/volk)
add_subdirectory(thirdparty/glm)
add_subdirectory(thirdparty/vma)
add_subdirectory(thirdparty/tinyobjloader)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/types.hpp
    src/vk_types.hpp

    src/scene/mesh.hpp
    src/scene/model.hpp
    src/scene/loader.hpp
    src/scene/loader.cpp

    src/platform/vma_impl.cpp
)

target_include_directories(${PROJECT_NAME}
PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(${PROJECT_NAME}
PRIVATE
    glfw
    volk
    glm
    VulkanMemoryAllocator
    tinyobjloader
)

target_compile_definitions(${PROJECT_NAME}
PRIVATE
    NOMINMAX
    GLFW_INCLUDE_NONE
    GLM_ENABLE_EXPERIMENTAL
)

target_precompile_headers(${PROJECT_NAME}
PRIVATE
    src/pch.hpp
)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        GLFW_EXPOSE_NATIVE_WIN32
    )
endif()

if(NOT MSVC)
    target_compile_options(${PROJECT_NAME}
    PRIVATE
        "-Wno-nullability-completeness"
    )
endif()

file(TO_CMAKE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/res RES_FOLDER)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/PathConfig.inl
    ${CMAKE_CURRENT_BINARY_DIR}/generated/PathConfig.inl
    @ONLY
)

find_program(GLSLC_EXE NAMES glslc HINTS Vulkan::glslc)

set(SHADER_SRC_DIR ${RES_FOLDER}/Shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/Shaders)

function(compile_shaders_target target_name)
    file(GLOB_RECURSE SHADER_SOURCES
        ${SHADER_SRC_DIR}/*.vert
        ${SHADER_SRC_DIR}/*.frag
        ${SHADER_SRC_DIR}/*.comp
        ${SHADER_SRC_DIR}/*.geom
        ${SHADER_SRC_DIR}/*.tesc
        ${SHADER_SRC_DIR}/*.tese
        ${SHADER_SRC_DIR}/*.mesh
        ${SHADER_SRC_DIR}/*.task
        ${SHADER_SRC_DIR}/*.rgen
        ${SHADER_SRC_DIR}/*.rchit
        ${SHADER_SRC_DIR}/*.rmiss
    )

    if(NOT SHADER_SOURCES)
        message(WARNING "No shader sources found in ${SHADER_SRC_DIR}")
        return()
    endif()

    add_custom_command(
        OUTPUT ${SHADER_BIN_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
        COMMENT "Creating shader output directory: ${SHADER_BIN_DIR}"
    )

    set(SPV_SHADERS "")

    foreach(shader_source ${SHADER_SOURCES})
        get_filename_component(shader_name ${shader_source} NAME)
        set(spv_output ${SHADER_BIN_DIR}/${shader_name}.spv)

        add_custom_command(
            OUTPUT ${spv_output}
            COMMAND ${GLSLC_EXE}
                --target-spv=spv1.4
                -o ${spv_output}
                ${shader_source}
            DEPENDS ${shader_source} ${SHADER_BIN_DIR}
            COMMENT "Compiling shader: ${shader_name}"
            VERBATIM
        )

        list(APPEND SPV_SHADERS ${spv_output})
    endforeach()

    add_custom_target(${target_name}
        DEPENDS ${SPV_SHADERS}
        COMMENT "Building all shaders"
    )

    add_dependencies(${PROJECT_NAME} ${target_name})

    target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${SHADER_BIN_DIR}
    )
endfunction()

compile_shaders_target(compile_shaders)

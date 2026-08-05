cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(ExplorationHorizon LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fix MSVC PDB locking error (C1090) by forcing /Z7 and synchronous compiler file writing (/FS)
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded" CACHE STRING "" FORCE)
    add_compile_options("/Z7" "/FS")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REPLACE "/ZI" "/Z7" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/ZI" "/Z7" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/ZI" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/ZI" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

include(FetchContent)

# Fetch GLFW (Static library)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

# Fetch GLM (Header-Only Interface)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

include_directories(include)

add_executable(ExplorationHorizon
    src/main.cpp
    src/Window.cpp
    src/Shader.cpp
    src/Camera.cpp
    src/Texture.cpp
    src/stb_image.cpp
    src/Block.cpp
    src/World.cpp
    src/Hotbar.cpp
    src/CloudRenderer.cpp
    src/glad.c
    resource.rc
)

target_include_directories(ExplorationHorizon PRIVATE include)
target_link_libraries(ExplorationHorizon PRIVATE glfw glm::glm)

if(WIN32)
    target_link_libraries(ExplorationHorizon PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(ExplorationHorizon PRIVATE "-framework OpenGL")
else()
    target_link_libraries(ExplorationHorizon PRIVATE GL)
endif()

# Post-Build Asset Copy Rule:
# Automatically copy the assets/ folder to the output directory alongside ExplorationHorizon.exe
add_custom_command(TARGET ExplorationHorizon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:ExplorationHorizon>/assets)

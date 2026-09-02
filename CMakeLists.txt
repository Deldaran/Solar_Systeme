cmake_minimum_required(VERSION 3.20)
project(SolarSystem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---- GLFW ----
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ---- GLM ----
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ---- ImGui ----
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# ---- Executable ----
add_executable(SolarSystem
  src/main.cpp
  ${IMGUI_SOURCES}
)

target_include_directories(SolarSystem PRIVATE
  src/
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(SolarSystem PRIVATE glfw glm::glm)

# ---- OpenGL ----
find_package(OpenGL REQUIRED)
target_link_libraries(SolarSystem PRIVATE OpenGL::GL)

# ---- macOS ----
if(APPLE)
  target_compile_definitions(SolarSystem PRIVATE GL_SILENCE_DEPRECATION)
  target_link_libraries(SolarSystem PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# ---- Optimisations Release ----
# -march=native produit un binaire non distribuable (illegal instruction sur
# une autre machine) : opt-in explicite via -DNATIVE_ARCH=ON.
option(NATIVE_ARCH "Optimiser pour le CPU de la machine de build" OFF)

target_compile_options(SolarSystem PRIVATE
  $<$<CONFIG:Release>:-O3>
  $<$<AND:$<CONFIG:Release>,$<BOOL:${NATIVE_ARCH}>>:-march=native>
)

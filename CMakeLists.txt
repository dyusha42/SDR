cmake_minimum_required(VERSION 3.16)
project(PlutoSDR CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(WITH_SDR "Работа с PlutoSDR через SoapySDR" ON)
option(WITH_GUI "Живое окно ImGui/ImPlot (lab4_rx --gui)" ON)

include_directories(include)
find_package(Threads REQUIRED)

# библиотека обработки сигналов
add_library(sdrdsp STATIC
  src/common.cpp
  src/frame.cpp
  src/tx_dsp.cpp
  src/rx_dsp.cpp
  src/iq_io.cpp
  src/live_data.cpp
  src/cli.cpp
  src/runtime.cpp)
target_compile_options(sdrdsp PRIVATE -Wall -Wextra)

# поиск SoapySDR
set(HAVE_SOAPY FALSE)
if(WITH_SDR)
  find_package(SoapySDR CONFIG QUIET)
  if(SoapySDR_FOUND)
    set(HAVE_SOAPY TRUE)
    message(STATUS "SoapySDR найден: работа с PlutoSDR включена")
  else()
    message(WARNING "SoapySDR не найден: сборка без поддержки железа (останутся --dump, --file, --sim)")
  endif()
endif()

if(HAVE_SOAPY)
  add_library(sdrdev STATIC src/sdr_device.cpp)
  target_include_directories(sdrdev PUBLIC ${SoapySDR_INCLUDE_DIRS})
  target_link_libraries(sdrdev PUBLIC ${SoapySDR_LIBRARIES})
  target_compile_options(sdrdev PRIVATE -Wall -Wextra)
endif()

# живое окно: нужны SDL2, OpenGL и исходники third_party/imgui, third_party/implot
set(HAVE_GUI FALSE)
if(WITH_GUI AND HAVE_SOAPY)
  set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
  set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/third_party/implot)
  find_package(SDL2 QUIET)
  find_package(OpenGL QUIET)
  if(SDL2_FOUND AND OPENGL_FOUND AND EXISTS ${IMGUI_DIR}/imgui.cpp AND EXISTS ${IMPLOT_DIR}/implot.cpp)
    set(HAVE_GUI TRUE)
    message(STATUS "Окно ImGui включено (lab4_rx --gui)")

    add_library(imgui_lib STATIC
      ${IMGUI_DIR}/imgui.cpp
      ${IMGUI_DIR}/imgui_draw.cpp
      ${IMGUI_DIR}/imgui_tables.cpp
      ${IMGUI_DIR}/imgui_widgets.cpp
      ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
      ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui_lib PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${SDL2_INCLUDE_DIRS})

    add_library(implot_lib STATIC
      ${IMPLOT_DIR}/implot.cpp
      ${IMPLOT_DIR}/implot_items.cpp)
    target_include_directories(implot_lib PUBLIC ${IMPLOT_DIR})
    target_link_libraries(implot_lib PUBLIC imgui_lib)

    add_library(sdrgui STATIC src/gui.cpp src/tribal_art.cpp)
    target_include_directories(sdrgui PUBLIC ${SDL2_INCLUDE_DIRS})
    if(TARGET SDL2::SDL2)
      target_link_libraries(sdrgui PUBLIC implot_lib SDL2::SDL2 ${OPENGL_LIBRARIES} Threads::Threads)
    else()
      target_link_libraries(sdrgui PUBLIC implot_lib ${SDL2_LIBRARIES} ${OPENGL_LIBRARIES} Threads::Threads)
    endif()
  else()
    message(STATUS "Окно ImGui отключено: нужны SDL2, OpenGL, third_party/imgui и third_party/implot (см. README)")
  endif()
endif()

function(add_lab name)
  add_executable(${name} src/${name}.cpp)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  target_link_libraries(${name} PRIVATE sdrdsp Threads::Threads)
  if(HAVE_SOAPY)
    target_compile_definitions(${name} PRIVATE HAVE_SDR)
    target_link_libraries(${name} PRIVATE sdrdev)
  endif()
  if(HAVE_GUI)
    target_compile_definitions(${name} PRIVATE HAVE_GUI)
    target_link_libraries(${name} PRIVATE sdrgui)
  endif()
endfunction()

add_lab(lab2_link)
add_lab(lab3_tx)
add_lab(lab4_rx)

add_executable(channel_sim src/channel_sim.cpp)
target_compile_options(channel_sim PRIVATE -Wall -Wextra)
target_link_libraries(channel_sim PRIVATE sdrdsp)

# автотест без железа
enable_testing()
add_test(NAME simulation
  COMMAND ${CMAKE_SOURCE_DIR}/tests/run_sim_test.sh $<TARGET_FILE:lab3_tx> $<TARGET_FILE:channel_sim> $<TARGET_FILE:lab4_rx>)

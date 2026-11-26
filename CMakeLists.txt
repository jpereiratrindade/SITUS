# SITUS — Sistema de Conhecimento Territorial
# Copyright (C) 2026 José Pedro Trindade
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

project(situs VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Verificação real de suporte a C++26 (sem rebaixamento silencioso)
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-std=c++26")
check_cxx_source_compiles("
#include <expected>
#include <ranges>
#include <span>
#if __cplusplus < 202400L
#error C++26 nao ativo
#endif
int main() {
    std::expected<int, int> e = 42;
    return *e == 42 ? 0 : 1;
}
" SITUS_HAS_CXX26)
unset(CMAKE_REQUIRED_FLAGS)

if(NOT SITUS_HAS_CXX26)
  message(FATAL_ERROR "O compilador nao suporta C++26 efetivo. Nao e permitido rebaixar o padrao.")
endif()

option(SITUS_BUILD_WASM "Compilar apenas o nucleo portavel e fronteira Wasm (Emscripten)" OFF)

# Núcleo portável compartilhado entre servidor nativo e navegador Wasm
add_library(situs_core STATIC src/core/synthesis.cpp)
target_include_directories(situs_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(situs_core PRIVATE -Wall -Wextra -Wpedantic)

if(SITUS_BUILD_WASM)
  find_package(nlohmann_json CONFIG QUIET)
  if(NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INCLUDE_DIR NAMES nlohmann/json.hpp HINTS /usr/include /usr/local/include NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/wasm_deps/include")
    file(COPY "${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann" DESTINATION "${CMAKE_BINARY_DIR}/wasm_deps/include")
  endif()

  add_executable(situs_core_wasm src/wasm/wasm_entry.cpp)
  target_link_libraries(situs_core_wasm PRIVATE situs_core)
  if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(situs_core_wasm PRIVATE nlohmann_json::nlohmann_json)
  else()
    target_include_directories(situs_core_wasm PRIVATE "${CMAKE_BINARY_DIR}/wasm_deps/include")
  endif()
  target_compile_options(situs_core_wasm PRIVATE -Wall -Wextra -Wpedantic -O2)
  set_target_properties(situs_core_wasm PROPERTIES
    OUTPUT_NAME "situs_core"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web/wasm"
  )
  target_link_options(situs_core_wasm PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createSitusCoreModule"
    "-sEXPORTED_FUNCTIONS=['_situs_wasm_version','_situs_wasm_run_synthesis','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sSTACK_SIZE=1048576"
    "-sINITIAL_MEMORY=16777216"
    "-sMAXIMUM_MEMORY=134217728"
    "-sENVIRONMENT=worker,node"
  )

  include(CTest)
  if(BUILD_TESTING)
    add_executable(situs_wasm_core_tests tests/execution/test_synthesis_contract.cpp)
    target_link_libraries(situs_wasm_core_tests PRIVATE situs_core)
    add_test(NAME wasm_core_synthesis_contract COMMAND situs_wasm_core_tests)
  endif()
  return()
endif()

# Destino Nativo
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(BOOST_INCLUDE_DIR NAMES boost/asio.hpp REQUIRED)

pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)
pkg_check_modules(GDAL REQUIRED IMPORTED_TARGET gdal)
pkg_check_modules(GEOS REQUIRED IMPORTED_TARGET geos)
pkg_check_modules(PROJ REQUIRED IMPORTED_TARGET proj)

add_library(situs_policy STATIC src/policy/category_a.cpp)
target_include_directories(situs_policy PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(situs_policy PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(situs_policy PRIVATE -Wall -Wextra -Wpedantic)

add_library(situs_execution STATIC src/execution/contracts.cpp)
target_include_directories(situs_execution PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(situs_execution PUBLIC situs_core nlohmann_json::nlohmann_json OpenSSL::Crypto)
target_compile_options(situs_execution PRIVATE -Wall -Wextra -Wpedantic)

add_library(situs_spatial STATIC
  src/spatial/postgis_store.cpp
  src/spatial/raster_service.cpp
)
target_include_directories(situs_spatial PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(situs_spatial PUBLIC
  situs_policy
  nlohmann_json::nlohmann_json
  PkgConfig::LIBPQ
  PkgConfig::GDAL
  PkgConfig::GEOS
  PkgConfig::PROJ
)
target_compile_options(situs_spatial PRIVATE -Wall -Wextra -Wpedantic)

add_library(situs_application STATIC src/application/service.cpp)
target_include_directories(situs_application PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(situs_application PUBLIC situs_spatial situs_execution)
target_compile_options(situs_application PRIVATE -Wall -Wextra -Wpedantic)

add_library(situs_http STATIC src/http/server.cpp)
target_include_directories(situs_http PUBLIC ${CMAKE_SOURCE_DIR}/include ${BOOST_INCLUDE_DIR})
target_link_libraries(situs_http PUBLIC situs_application Threads::Threads)
target_compile_definitions(situs_http PUBLIC BOOST_ERROR_CODE_HEADER_ONLY BOOST_SYSTEM_NO_DEPRECATED)
target_compile_options(situs_http PRIVATE -Wall -Wextra -Wpedantic)

add_executable(situs src/cli/main.cpp)
target_link_libraries(situs PRIVATE situs_http OpenSSL::Crypto)
target_compile_options(situs PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
  add_executable(situs_policy_tests tests/unit/test_policy.cpp)
  target_link_libraries(situs_policy_tests PRIVATE situs_policy)
  target_compile_options(situs_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME policy_tests COMMAND situs_policy_tests)

  add_executable(situs_synthesis_tests tests/execution/test_synthesis_contract.cpp)
  target_link_libraries(situs_synthesis_tests PRIVATE situs_core)
  target_compile_options(situs_synthesis_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME synthesis_contract_tests COMMAND situs_synthesis_tests)
endif()

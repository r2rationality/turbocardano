set(Boost_NO_WARN_NEW_VERSIONS 1)
if (WIN32)
    find_package(Boost 1.83 REQUIRED COMPONENTS json url coroutine thread stacktrace_windbg)
elseif(APPLE)
    find_package(Boost 1.83 REQUIRED COMPONENTS json url coroutine thread stacktrace_noop)
else()
    find_package(Boost 1.83 REQUIRED COMPONENTS json url coroutine thread stacktrace_backtrace)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(FMT REQUIRED fmt>=8.1.1)
pkg_check_modules(SODIUM REQUIRED libsodium>=1.0.18)
pkg_check_modules(ZSTD REQUIRED libzstd>=1.4.8)
pkg_search_module(BOTAN REQUIRED botan-3>=3.0.0 botan-2>=2.19.0)
find_library(SECP256K1_LIBRARY_PATH secp256k1)
file(GLOB_RECURSE LIB_SRCS CONFIGURE_DEPENDS turbo/*.cpp)
list(FILTER LIB_SRCS EXCLUDE REGEX "\\.test\\.cpp$")
list(FILTER LIB_SRCS EXCLUDE REGEX "\\.bench\\.cpp$")
list(FILTER LIB_SRCS EXCLUDE REGEX "\\.fuzz\\.cpp$")
add_library(lib OBJECT ${LIB_SRCS})
if (CMAKE_BUILD_TYPE STREQUAL "Coverage"
        AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 21
        AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 22)
    # Clang 21.1.8 crashes in LICM at -O3 while optimizing an inlined
    # std::copy in pool-certs.cpp. Retain optimization while avoiding the
    # affected -O3 pipeline.
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/turbo/cli-debug/pool-certs.cpp"
        APPEND PROPERTY COMPILE_OPTIONS -O2)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13
        AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14
        AND FMT_VERSION VERSION_LESS 10)
    # fmt 9's bigint formatter triggers false positive bounds diagnostics in optimized GCC 13 builds.
    target_compile_options(lib PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-array-bounds>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-stringop-overflow>
    )
endif()
# Keep the PCH stable: project headers change frequently and would turn a
# local header edit into a full rebuild. Consumers may explicitly reuse this
# PCH, but it is not propagated as part of lib's public interface.
target_precompile_headers(lib PRIVATE
    <algorithm>
    <array>
    <atomic>
    <chrono>
    <concepts>
    <cstdint>
    <filesystem>
    <functional>
    <map>
    <memory>
    <mutex>
    <optional>
    <span>
    <string>
    <string_view>
    <unordered_map>
    <utility>
    <variant>
    <vector>
    <boost/container/flat_map.hpp>
    <boost/container/flat_set.hpp>
    <boost/json.hpp>
    <fmt/format.h>
    <spdlog/spdlog.h>
)
target_include_directories(lib PUBLIC .)
target_include_directories(lib PUBLIC ${BOTAN_INCLUDE_DIRS})
target_link_libraries(lib PUBLIC Boost::json Boost::coroutine Boost::url Boost::thread
        ${Boost_LIBRARIES}
        ${BOTAN_LIBRARIES}
        ${FMT_LIBRARIES}
        ${SECP256K1_LIBRARY_PATH}
        ${ZSTD_LIBRARIES}
)
if (WIN32)
    target_compile_options(lib PUBLIC "-DBOOST_STACKTRACE_USE_WINDBG")
    target_compile_options(lib PUBLIC "-DBOOST_STACKTRACE_LINK")
    target_link_libraries(lib PUBLIC Boost::stacktrace_windbg)
elseif(APPLE)
    target_compile_options(lib PUBLIC "-DBOOST_STACKTRACE_USE_NOOP")
    target_compile_options(lib PUBLIC "-DBOOST_STACKTRACE_LINK")
    target_link_libraries(lib PUBLIC Boost::stacktrace_noop dl)
else()
    target_compile_options(lib PUBLIC "-DBOOST_STACKTRACE_USE_BACKTRACE")
    target_compile_options(lib PUBLIC "-DBOOST_STACKTRACE_LINK")
    target_link_libraries(lib PUBLIC Boost::stacktrace_backtrace dl)
endif()
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(lib PUBLIC spdlog::spdlog)
if (MSVC)
    find_library(SECP256K1_PRE_LIBRARY_PATH secp256k1_precomputed)
    target_link_libraries(lib PUBLIC libsodium.lib)
    target_link_libraries(lib PUBLIC ${SECP256K1_PRE_LIBRARY_PATH})
else()
    target_link_libraries(lib PUBLIC ${SODIUM_LIBRARIES})
endif()
target_link_directories(lib PUBLIC
    ${Boost_LIBRARY_DIRS}
    ${FMT_LIBRARY_DIRS}
    ${SECP256K1_LIBRARY_DIRS}
    ${SODIUM_LIBRARY_DIRS}
    ${ZSTD_LIBRARY_DIRS}
)
if (WIN32)
    target_compile_options(lib PUBLIC -DNOMINMAX=1)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_libraries(lib PUBLIC atomic)
    endif()
endif()
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(lib PUBLIC "-D_GNU_SOURCE")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 18.0)
        set(MY_CLANG_VERSION ${CMAKE_CXX_COMPILER_VERSION})
        string(REPLACE "." ";" MY_CLANG_VERSION_LIST ${MY_CLANG_VERSION})
        list(GET MY_CLANG_VERSION_LIST 0 MY_CLANG_VERSION_MAJOR)
    	target_link_directories(lib PUBLIC "/opt/homebrew/opt/llvm@${MY_CLANG_VERSION_MAJOR}/lib/c++")
    	target_link_directories(lib PUBLIC "/opt/homebrew/opt/llvm@${MY_CLANG_VERSION_MAJOR}/lib")
    	target_link_directories(lib PUBLIC "/opt/homebrew/opt/llvm@${MY_CLANG_VERSION_MAJOR}/lib/unwind")
    	target_link_libraries(lib PUBLIC "-lunwind")
    endif()
endif()
set_target_properties(lib PROPERTIES LINKER_LANGUAGE CXX)
if (MINGW)
    target_link_libraries(lib PUBLIC "ws2_32")
    target_link_libraries(lib PUBLIC "wsock32")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 20
        AND FMT_VERSION VERSION_LESS 11.2)
    target_compile_definitions(lib PUBLIC FMT_CONSTEVAL=)
    message("FMT_USE_CONSTEVAL=0")
endif()

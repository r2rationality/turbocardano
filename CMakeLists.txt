cmake_minimum_required(VERSION 3.28)
if(POLICY CMP0168)
    cmake_policy(SET CMP0168 NEW)
endif()
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT IS_MULTI_CONFIG)
    if(CMAKE_HOST_WIN32)
        set(DEFAULT_BUILD_TYPE RelWithDebInfo)
    else()
        set(DEFAULT_BUILD_TYPE Release)
    endif()
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}"
        CACHE STRING "Choose the type of build: Release RelWithDebInfo Debug Coverage Profile Valgrind.")
    unset(DEFAULT_BUILD_TYPE)
endif()
project(DT C CXX ASM)
set(CMAKE_PROJECT_VERSION_MAJOR 0)
set(CMAKE_PROJECT_VERSION_MINOR 9)
set(CMAKE_PROJECT_VERSION_PATCH 0)
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed")
endif()
set(COVERAGE_ARGS "" CACHE STRING "select specific unit tests to run for test coverage evaluations")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
# This project does not use C++ modules. Avoid the extra compiler pass CMake
# otherwise adds to discover module dependencies for every translation unit.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall)
    add_compile_options(-Wextra)
    add_compile_options(-Wthread-safety)
    add_compile_options(-Wno-missing-braces)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++26>)
    add_compile_options(-Wextra)
    add_compile_options(-Wpedantic)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-changes-meaning>)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
if (MSVC)
    string(REPLACE "/Ob1" "/Ob2"
        CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/Ob1" "/Ob2"
        CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:/bigobj>
        $<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>
    )
    add_link_options(/Stack:16777216)
    enable_language(ASM_MASM)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:ASM_MASM>:/nologo>
        $<$<COMPILE_LANGUAGE:ASM_MASM>:/quiet>
    )
    set(OPTIMIZED_CONFIG "$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>")
    add_compile_options(
        $<$<AND:${OPTIMIZED_CONFIG},$<COMPILE_LANGUAGE:C,CXX>>:/GL>
        $<$<AND:${OPTIMIZED_CONFIG},$<COMPILE_LANGUAGE:C,CXX>>:/arch:AVX2>
    )
    add_link_options(
        $<${OPTIMIZED_CONFIG}:/LTCG>
        $<${OPTIMIZED_CONFIG}:/INCREMENTAL:NO>
        $<${OPTIMIZED_CONFIG}:/OPT:REF>
        $<${OPTIMIZED_CONFIG}:/OPT:ICF>
    )
endif()

if (MINGW)
    add_compile_options(-Wa)
    add_compile_options(-mbig-obj)
    list(APPEND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES /mingw64/include)
endif()
if(UNIX)
    add_compile_options(-g)
    add_link_options(-g)
    if(NOT APPLE)
        link_libraries("atomic")
    endif()
endif()
if(CMAKE_BUILD_TYPE STREQUAL Sanitize)
    message("Sanitize build")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        if (NOT DEFINED SANITIZER)
            set(SANITIZER "address,undefined")
        endif()
        add_compile_options(-g -O3 -fno-omit-frame-pointer -ftrivial-auto-var-init=pattern "-fsanitize=${SANITIZER}" "-fno-sanitize-recover=all")
        add_link_options("-fsanitize=${SANITIZER}" "-static-libsan")
    else()
        message(FATAL_ERROR "Sanitize builds are only supported with Clang")
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL Fuzzer)
    message("Fuzzer build")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(SANITIZER "fuzzer,address,undefined")
        add_compile_options(-g)
        add_compile_options(-O3)
        add_compile_options(-fno-omit-frame-pointer)
        add_compile_options("-fsanitize=${SANITIZER}")
        add_link_options("-fsanitize=${SANITIZER}")
        add_link_options("-static-libsan")
    else()
        message(FATAL_ERROR "Fuzzer builds are only supported with Clang")
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL Coverage)
    message("Coverage build")
    if (NOT DEFINED SANITIZER)
        set(SANITIZER "address,undefined")
    endif()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options("-ftime-trace" "-fprofile-instr-generate" "-fcoverage-mapping" "-fprofile-update=atomic")
        add_compile_options(-g -O3 -fno-omit-frame-pointer -ftrivial-auto-var-init=pattern "-fsanitize=${SANITIZER}" "-fno-sanitize-recover=all")
        add_link_options("-fprofile-instr-generate" "-fcoverage-mapping" "-g")
        add_link_options("-fsanitize=${SANITIZER}" "-static-libsan")
        find_program(BASH_EXECUTABLE NAMES bash REQUIRED)
        add_custom_target(coverage
            COMMENT "Running test coverage ..."
            COMMAND ${BASH_EXECUTABLE} "${CMAKE_SOURCE_DIR}/test/gen-coverage.sh"
                "${COVERAGE_ARGS}" "tmp/coverage" "${CMAKE_BINARY_DIR}"
            DEPENDS tada-test
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM
        )
    else()
        message(FATAL_ERROR "Coverage builds are only supported with Clang but got ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

if (WIN32)
    add_compile_options(-DMI_BUILD_SHARED -DMI_OVERRIDE)
    include(FetchContent)
    FetchContent_Declare(
            mimalloc
            GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
            GIT_TAG "v3.3.2" # https://github.com/ericniebler/range-v3/releases
            GIT_SHALLOW TRUE
            GIT_PROGRESS ON
    )
    FetchContent_MakeAvailable(mimalloc)
    FetchContent_GetProperties(mimalloc)
endif()

add_subdirectory(3rdparty)
add_subdirectory(lib)
target_link_libraries(lib PUBLIC 3rdparty 3rdparty-cpp)

add_executable(tada src/tada.cpp)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)
install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin)
install(TARGETS tada RUNTIME DESTINATION bin)
install(DIRECTORY etc/ DESTINATION etc)
if (WIN32)
    if (MINGW)
        add_custom_command(TARGET tada POST_BUILD COMMAND ldd ${CMAKE_BINARY_DIR}/tada.exe | awk '{ print $$3 }' | grep -vi system > ${CMAKE_BINARY_DIR}/dlls.txt)
        add_custom_command(TARGET tada POST_BUILD COMMAND cygpath -w -f ${CMAKE_BINARY_DIR}/dlls.txt > ${CMAKE_BINARY_DIR}/dlls-win-path.txt)
        install(CODE [[
            file(READ $<TARGET_FILE_DIR:tada>/dlls-win-path.txt DLLS)
            string(REPLACE "\n" ";" DLLS_LIST ${DLLS})
            file(COPY ${DLLS_LIST} DESTINATION $<INSTALL_PREFIX>/bin)
        ]])
    else()
        file(TO_NATIVE_PATH ${CMAKE_BINARY_DIR} BIN_DIR_WIN)
        add_custom_command(TARGET tada POST_BUILD COMMAND dir /b ${BIN_DIR_WIN}\\*.dll > ${BIN_DIR_WIN}\\dlls.txt COMMAND_EXPAND_LISTS)
        install(CODE [[
            file(READ $<TARGET_FILE_DIR:tada>/dlls.txt DLLS)
            string(REPLACE "\n" ";" DLLS_LIST ${DLLS})
            foreach(lib ${DLLS_LIST})
                LIST(APPEND DLLS_WITH_PATH "$<TARGET_FILE_DIR:tada>/${lib}")
            endforeach()
            file(COPY ${DLLS_WITH_PATH} DESTINATION $<INSTALL_PREFIX>/bin)
        ]])
    endif()
endif()

file(GLOB ITEST_SRCS test/*.cpp)
foreach(ITEST_SRC IN LISTS ITEST_SRCS)
    cmake_path(GET ITEST_SRC STEM ITEST)
    add_executable(${ITEST} ${ITEST_SRC})
    target_link_libraries(${ITEST} PRIVATE lib 3rdparty 3rdparty-cpp)
endforeach()

file(GLOB_RECURSE TEST_SRCS CONFIGURE_DEPENDS lib/turbo/*.test.cpp)
add_executable(tada-test src/run-test.cpp ${TEST_SRCS})
target_precompile_headers(tada-test REUSE_FROM lib)

file(GLOB_RECURSE BENCH_SRCS lib/turbo/*.bench.cpp)
add_executable(tada-bench src/run-test.cpp ${BENCH_SRCS})
target_precompile_headers(tada-bench REUSE_FROM lib)

foreach(EXE tada tada-test tada-bench)
    if (WIN32)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(MIMALLOC_DLLNAME "mimalloc-debug.dll")
        else()
            set(MIMALLOC_DLLNAME "mimalloc.dll")
        endif()
        message("${EXE} ${mimalloc_BINARY_DIR}/${MIMALLOC_DLLNAME}")
        target_link_libraries(${EXE} PRIVATE mimalloc lib 3rdparty 3rdparty-cpp)
        add_custom_command(TARGET ${EXE}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${mimalloc_BINARY_DIR}/mimalloc-redirect.dll "${CMAKE_BINARY_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${mimalloc_BINARY_DIR}/${MIMALLOC_DLLNAME} "${CMAKE_BINARY_DIR}"
            COMMENT "Copy dll file to ${CMAKE_BINARY_DIR} directory" VERBATIM
        )
    else()
        message("${EXE} no mimalloc")
        target_link_libraries(${EXE} PRIVATE lib 3rdparty 3rdparty-cpp)
    endif()
endforeach()

message("copying configs")
file(COPY etc DESTINATION .)
if (EXISTS ${CMAKE_SOURCE_DIR}/publisher)
    file(COPY publisher DESTINATION .)
endif()
message("copying test data")
file(COPY data DESTINATION .)
set(PLUTUS_CONFORMANCE_SOURCE
    "${CMAKE_SOURCE_DIR}/3rdparty/plutus/plutus-conformance/test-cases/uplc/evaluation")
if(NOT IS_DIRECTORY "${PLUTUS_CONFORMANCE_SOURCE}")
    message(FATAL_ERROR "Plutus conformance data is unavailable; initialize the 3rdparty/plutus submodule")
endif()

if(CMAKE_BUILD_TYPE STREQUAL Fuzzer)
    file(GLOB_RECURSE FUZZ_SRCS lib/turbo/*.fuzz.cpp)
    cmake_path(SET FUZZ_BASE_DIR NORMALIZE "./lib/turbo")
    cmake_path(ABSOLUTE_PATH FUZZ_BASE_DIR)
    message("fuzz base dir ${FUZZ_BASE_DIR}")
    foreach(FUZZ_SRC IN LISTS FUZZ_SRCS)
        cmake_path(GET FUZZ_SRC STEM FUZZ_STEM)
        cmake_path(GET FUZZ_SRC PARENT_PATH FUZZ_PARENT)
        cmake_path(RELATIVE_PATH FUZZ_PARENT BASE_DIRECTORY ${FUZZ_BASE_DIR} OUTPUT_VARIABLE FUZZ_REL_PATH)
        string(REPLACE "/" "-" FUZZ_NAME ${FUZZ_REL_PATH})
        set(FUZZ_BIN "fuzz-${FUZZ_NAME}-${FUZZ_STEM}")
        message("fuzz target ${FUZZ_BIN}")
        add_executable(${FUZZ_BIN} ${FUZZ_SRC})
        target_link_libraries(${FUZZ_BIN} PRIVATE lib 3rdparty 3rdparty-cpp)
    endforeach()
endif()

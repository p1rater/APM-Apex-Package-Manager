cmake_minimum_required(VERSION 3.16)
project(apex VERSION 1.0.0 LANGUAGES C CXX)

# C17 + C++17
set(CMAKE_C_STANDARD   17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags — warnings as documentation, not as errors in release
set(COMMON_FLAGS "-Wall -Wextra -Wpedantic -Wformat=2 -Wno-unused-parameter")
set(CMAKE_C_FLAGS_DEBUG   "${COMMON_FLAGS} -g3 -fsanitize=address,undefined")
set(CMAKE_CXX_FLAGS_DEBUG "${COMMON_FLAGS} -g3 -fsanitize=address,undefined")
set(CMAKE_C_FLAGS_RELEASE   "${COMMON_FLAGS} -O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${COMMON_FLAGS} -O2 -DNDEBUG")

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCURL  REQUIRED libcurl)
pkg_check_modules(LIBGCRYPT REQUIRED libgcrypt)
pkg_check_modules(LIBZSTD  REQUIRED libzstd)
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)

# Optional: gpgme for better GPG support
pkg_check_modules(GPGME gpgme)

# Include dirs
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${LIBCURL_INCLUDE_DIRS}
    ${LIBGCRYPT_INCLUDE_DIRS}
    ${LIBZSTD_INCLUDE_DIRS}
    ${LIBARCHIVE_INCLUDE_DIRS}
)

# ── libapex — the core library ───────────────────────────────────────────────
set(APEX_CORE_SOURCES
    src/util/util.c
    src/core/version.c
    src/solver/solver.c
    src/db/db.c
    src/net/net.c
    src/net/mirror.c
    src/pkg/pkg.c
    src/pkg/archive.c
    src/pkg/hooks.c
    src/crypto/crypto.c
    src/crypto/sha256.c
)

add_library(apexcore STATIC ${APEX_CORE_SOURCES})

target_link_libraries(apexcore
    ${LIBCURL_LIBRARIES}
    ${LIBGCRYPT_LIBRARIES}
    ${LIBZSTD_LIBRARIES}
    ${LIBARCHIVE_LIBRARIES}
)

if(GPGME_FOUND)
    target_link_libraries(apexcore ${GPGME_LIBRARIES})
    target_compile_definitions(apexcore PRIVATE HAVE_GPGME=1)
endif()

# ── apex — the CLI binary ────────────────────────────────────────────────────
add_executable(apex src/ui/main.cpp)
target_link_libraries(apex apexcore pthread)

# ── apexbuild — package build tool ──────────────────────────────────────────
add_executable(apexbuild src/ui/apexbuild.cpp)
target_link_libraries(apexbuild apexcore)

# ── Install rules ────────────────────────────────────────────────────────────
install(TARGETS apex apexbuild DESTINATION bin)
install(FILES
    man/apex.1
    man/apexbuild.1
    DESTINATION share/man/man1
    OPTIONAL
)
install(FILES
    completions/apex.bash
    DESTINATION share/bash-completion/completions
    OPTIONAL
)
install(FILES
    completions/apex.zsh
    DESTINATION share/zsh/site-functions
    OPTIONAL
)

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

add_executable(test_version tests/test_version.c)
target_link_libraries(test_version apexcore)
add_test(NAME version COMMAND test_version)

add_executable(test_solver tests/test_solver.c)
target_link_libraries(test_solver apexcore)
add_test(NAME solver COMMAND test_solver)

add_executable(test_util tests/test_util.c)
target_link_libraries(test_util apexcore)
add_test(NAME util COMMAND test_util)

# ── Summary ──────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "  apex ${PROJECT_VERSION} — build configuration")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C          : ${CMAKE_C_COMPILER}")
message(STATUS "  C++        : ${CMAKE_CXX_COMPILER}")
message(STATUS "  GPGME      : ${GPGME_FOUND}")
message(STATUS "")

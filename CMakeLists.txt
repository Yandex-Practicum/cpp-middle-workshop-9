cmake_minimum_required(VERSION 3.20)
project(CryptoTerminal VERSION 0.1.0 LANGUAGES CXX)

# ============================================================================
# CMake Policies
# ============================================================================
# CMake 3.31 removed FindBoost module - use CONFIG mode for Boost
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

# ============================================================================
# C++ Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Опции сборки (передаются из conanfile.py)
# ============================================================================
option(WITH_TESTS "Build tests" ON)

# ============================================================================
# Компиляторные флаги
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fmax-errors=1 -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

# ============================================================================
# Поиск зависимостей через Conan
# ============================================================================
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json REQUIRED)
find_package(ftxui REQUIRED)
find_package(OpenSSL REQUIRED)

if(WITH_TESTS)
    find_package(GTest REQUIRED)
endif()

# ============================================================================
# stdexec через CPM.cmake (нет в Conan Center)
# ============================================================================
include(FetchContent)
FetchContent_Declare(
    CPM
    GIT_REPOSITORY https://github.com/cpm-cmake/CPM.cmake
    GIT_TAG origin/master
)
FetchContent_MakeAvailable(CPM)

CPMAddPackage(
    NAME stdexec
    GITHUB_REPOSITORY NVIDIA/stdexec
    GIT_TAG main
    OPTIONS
        "STDEXEC_BUILD_EXAMPLES OFF"
        "STDEXEC_BUILD_TESTS OFF"
)

# ============================================================================
# Исходные файлы
# ============================================================================
file(GLOB_RECURSE HEADER_FILES "${CMAKE_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE SRC_FILES "${CMAKE_SOURCE_DIR}/src/*.cpp")

# Исключаем main.cpp из библиотеки (будет отдельный executable)
list(FILTER SRC_FILES EXCLUDE REGEX ".*/main\\.cpp$")

# ============================================================================
# Основная библиотека проекта (business logic)
# ============================================================================
# Используем INTERFACE библиотеку т.к. реализация в заголовках (header-only)
add_library(crypto_core INTERFACE)

target_include_directories(crypto_core
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(crypto_core
    INTERFACE
        boost::boost            # Boost.Asio + Boost.Beast (header-only)
        nlohmann_json::nlohmann_json
        stdexec
        openssl::openssl            # SSL/TLS для WSS
)

target_compile_features(crypto_core INTERFACE cxx_std_23)

# ============================================================================
# Исполняемый файл (main + UI)
# ============================================================================
add_executable(crypto_terminal ${CMAKE_SOURCE_DIR}/src/main.cpp)

target_link_libraries(crypto_terminal
    PRIVATE
        crypto_core
        ftxui::screen
        ftxui::dom
        ftxui::component
)

# ============================================================================
# Пример использования WebSocket sender
# ============================================================================
add_executable(websocket_example ${CMAKE_SOURCE_DIR}/src/websocket_example.cpp)

target_link_libraries(websocket_example
    PRIVATE
        crypto_core
)

# ============================================================================
# Тесты
# ============================================================================
if(WITH_TESTS)
    enable_testing()

    file(GLOB_RECURSE TEST_SRC_FILES "${CMAKE_SOURCE_DIR}/tests/*.cpp")

    if(TEST_SRC_FILES)
        add_executable(crypto_terminal_tests ${TEST_SRC_FILES})

        target_link_libraries(crypto_terminal_tests
            PUBLIC
                crypto_core
            PRIVATE
                GTest::GTest
                GTest::Main
        )

        add_test(NAME crypto_terminal_tests COMMAND crypto_terminal_tests)

        # Для тестов тоже включаем санитайзеры если нужно
        if(WITH_SANITIZERS)
            target_compile_options(crypto_terminal_tests PRIVATE -fsanitize=address,undefined)
            target_link_options(crypto_terminal_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endif()

# ============================================================================
# Вывод информации о сборке
# ============================================================================
message(STATUS "")
message(STATUS "📦 Crypto Terminal Configuration:")
message(STATUS "   C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "   Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "   Tests: ${WITH_TESTS}")
message(STATUS "   Sanitizers: ${WITH_SANITIZERS}")
message(STATUS "   Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")

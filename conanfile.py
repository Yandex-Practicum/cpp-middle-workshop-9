from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps
from conan.tools.files import copy, rmdir
from conan.tools.layout import basic_layout
import os

class CryptoTerminalConan(ConanFile):
    name = "crypto_terminal"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    
    # Опции для гибкой настройки сборки
    options = {
        "with_tests": [True, False],
        "with_sanitizers": [True, False],
    }
    default_options = {
        "with_tests": True,
        "with_sanitizers": False,
    }

    def requirements(self):
        # Boost.Asio + Boost.Beast для networking и WebSocket
        # header_only=True т.к. нам нужны только заголовки (Asio/Beast — header-only библиотеки)
        self.requires("boost/1.84.0", options={
            "header_only": True,
            "without_system": False,  # Требуется для Boost.Asio
            "without_date_time": True,
            "without_filesystem": True,
            "without_regex": True,
            "without_serialization": True,
            "without_random": True,
            "without_thread": True,
            "without_chrono": True,
            "without_atomic": True,
            "without_container": False,  # Требуется для cobalt
            "without_context": True,
            "without_contract": True,
            "without_coroutine": True,
            "without_fiber": True,
            "without_graph": True,
            "without_graph_parallel": True,
            "without_iostreams": True,
            "without_json": True,
            "without_locale": True,
            "without_log": True,
            "without_math": True,
            "without_mpi": True,
            "without_nowide": True,
            "without_program_options": True,
            "without_python": True,
            "without_stacktrace": True,
            "without_test": True,
            "without_timer": True,
            "without_type_erasure": True,
            "without_wave": True,
        })

        # OpenSSL для SSL/TLS поддержки (требуется для WSS)
        self.requires("openssl/3.2.0")

        # JSON парсинг для сообщений биржи
        self.requires("nlohmann_json/3.11.3")

        # Terminal UI
        self.requires("ftxui/5.0.0")

        # stdexec (P2300) — Senders/Receivers модель
        # Подключается через CPM.cmake в CMakeLists.txt (нет в Conan Center)

        # Тестирование (опционально)
        if self.options.with_tests:
            self.requires("gtest/1.14.0")

    def layout(self):
        basic_layout(self, src_folder=".")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        # Передаём опции в CMake
        tc.variables["WITH_TESTS"] = self.options.with_tests
        tc.variables["WITH_SANITIZERS"] = self.options.with_sanitizers
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

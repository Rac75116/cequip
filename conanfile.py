from conan import ConanFile
from conan.tools.cmake import cmake_layout


class CequipConan(ConanFile):
    name = "cequip"
    version = "1.0.0"

    settings = "os", "arch", "compiler", "build_type"

    default_options = {
        "*:shared": False,
        "boost/*:without_cobalt": True,
    }

    requires = (
        "boost/1.90.0",
        "cli11/2.6.0",
        "spdlog/1.17.0",
    )

    generators = (
        "CMakeDeps",
        "CMakeToolchain",
    )

    def layout(self):
        # Keep all Conan/CMake generated files directly under the chosen output
        # folder (e.g. ./build), instead of nesting an extra ./build directory.
        cmake_layout(self, build_folder=".")

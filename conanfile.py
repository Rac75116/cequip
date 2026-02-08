from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeToolchain
import os
import shutil
import glob
import tomllib


def load_version():
    pyproject_path = os.path.join(os.path.dirname(__file__), "pyproject.toml")
    with open(pyproject_path, "rb") as f:
        pyproject_data = tomllib.load(f)
    return pyproject_data["project"]["version"]


class CequipConan(ConanFile):
    name = "cequip"
    authors = "Rac75116"
    version = load_version()
    license = "MIT"
    url = "https://github.com/Rac75116/cequip"

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

    generators = ("CMakeDeps",)

    def generate_license_file(self):
        dist_dir = os.path.join(self.export_sources_folder, "dist")
        if os.path.exists(dist_dir):
            shutil.rmtree(dist_dir)
        os.makedirs(dist_dir)
        license_output = os.path.join(dist_dir, "LICENSES.txt")

        with open(license_output, "w", encoding="utf-8") as outfile:
            with open(
                os.path.join(self.source_folder, "LICENSE"), "r", encoding="utf-8"
            ) as main_license:
                outfile.write(f"========== cequip/{self.version} ==========\n\n")
                outfile.write(main_license.read())
                if not main_license.read().endswith("\n"):
                    outfile.write("\n")
                outfile.write("\n")

            # Aggregate license texts from every dependency into one file.
            for dep in sorted(self.dependencies.values(), key=lambda d: d.ref.name):
                self.output.info(f"Collecting licenses from {dep.ref}")

                dep_label = f"{dep.ref.name}/{dep.ref.version}"
                license_dir = os.path.join(dep.package_folder, "licenses")
                license_files = sorted(glob.glob(os.path.join(license_dir, "*")))

                if not license_files:
                    continue

                outfile.write(f"========== {dep_label} ==========\n\n")

                for license_file in license_files:
                    outfile.write(f"-- {os.path.basename(license_file)} --\n")
                    with open(license_file, "rb") as lf:
                        content = lf.read().decode("utf-8", errors="replace")

                    outfile.write(content)
                    if not content.endswith("\n"):
                        outfile.write("\n")
                    outfile.write("\n")

                outfile.write("\n")

    def generate(self):

        self.generate_license_file()

        tc = CMakeToolchain(self)
        tc.variables["PROJECT_VERSION"] = self.version
        tc.generate()

    def layout(self):
        # Keep all Conan/CMake generated files directly under the chosen output
        # folder (e.g. ./build), instead of nesting an extra ./build directory.
        cmake_layout(self, build_folder=".")

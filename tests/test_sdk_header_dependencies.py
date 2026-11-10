"""An installed SDK layout change must invalidate an incremental app object."""
from pathlib import Path
import subprocess
import resource_workspace
import unittest

SOURCE = Path(__file__).resolve().parents[1]


class SdkHeaderDependencies(unittest.TestCase):
    def test_imported_sdk_header_rebuilds_consumer(self):
        with resource_workspace.temporary(SOURCE, "sdk-header-input-") as directory, \
             resource_workspace.build(SOURCE, "sdk-header-build-") as build:
            root = Path(directory)
            headers = root / "sdk"
            resource_workspace.provider.mkdir(headers)
            header = headers / "Layout.h"
            resource_workspace.provider.write_text(header, "struct Layout { char bytes[256]; };\n")
            resource_workspace.provider.write_text(root / "main.cpp",
                '#include <Layout.h>\n#include <cstdio>\n'
                'int main() { std::printf("%zu", sizeof(Layout)); }\n')
            resource_workspace.provider.write_text(root / "CMakeLists.txt", f'''cmake_minimum_required(VERSION 3.24)
project(HeaderTracking LANGUAGES CXX)
include("{(SOURCE / 'cmake/SdkHeaders.cmake').as_posix()}")
add_library(iiFixture INTERFACE IMPORTED)
set_target_properties(iiFixture PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${{CMAKE_CURRENT_SOURCE_DIR}}/sdk")
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE iiFixture)
''')
            def run(*args):
                return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
            run("cmake", "-S", str(root), "-B", str(build), "-G", "Ninja")
            run("cmake", "--build", str(build))
            self.assertEqual(run(str(build / ("consumer.exe" if __import__("os").name == "nt" else "consumer"))), "256")
            commands = run("ninja", "-C", str(build), "-t", "commands")
            self.assertNotIn("-isystem " + str(headers), commands)
            resource_workspace.provider.write_text(header, "struct Layout { char bytes[264]; };\n")
            run("cmake", "--build", str(build))
            self.assertEqual(run(str(build / ("consumer.exe" if __import__("os").name == "nt" else "consumer"))), "264")


if __name__ == "__main__":
    unittest.main()

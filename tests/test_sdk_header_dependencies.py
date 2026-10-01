"""An installed SDK layout change must invalidate an incremental app object."""
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]


class SdkHeaderDependencies(unittest.TestCase):
    def test_imported_sdk_header_rebuilds_consumer(self):
        with tempfile.TemporaryDirectory(dir=SOURCE / "build", prefix="sdk-header-") as directory:
            root = Path(directory)
            headers = root / "sdk"
            headers.mkdir()
            header = headers / "Layout.h"
            header.write_text("struct Layout { char bytes[256]; };\n")
            (root / "main.cpp").write_text(
                '#include <Layout.h>\n#include <cstdio>\n'
                'int main() { std::printf("%zu", sizeof(Layout)); }\n')
            (root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.24)
project(HeaderTracking LANGUAGES CXX)
include("{SOURCE / 'cmake/SdkHeaders.cmake'}")
add_library(iiFixture INTERFACE IMPORTED)
set_target_properties(iiFixture PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${{CMAKE_CURRENT_SOURCE_DIR}}/sdk")
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE iiFixture)
''')
            build = root / "build"
            def run(*args):
                return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
            run("cmake", "-S", str(root), "-B", str(build), "-G", "Ninja")
            run("cmake", "--build", str(build))
            self.assertEqual(run(str(build / "consumer")), "256")
            commands = run("ninja", "-C", str(build), "-t", "commands")
            self.assertNotIn("-isystem " + str(headers), commands)
            header.write_text("struct Layout { char bytes[264]; };\n")
            run("cmake", "--build", str(build))
            self.assertEqual(run(str(build / "consumer")), "264")


if __name__ == "__main__":
    unittest.main()

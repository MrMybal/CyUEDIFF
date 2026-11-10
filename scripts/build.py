"""Build and test with CMake; deduplicate Windows environment key casing."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--desktop', action='store_true', help='Build the Windows desktop host (requires --cyui or a cached checkout)')
parser.add_argument('--cyui', type=Path, help='Optional CyUI source checkout')
parser.add_argument('--oodle-sdk', type=Path, help='Optional host-provided Oodle Data SDK for a local codec adapter')
parser.add_argument('--no-git', action='store_true', help='Build only the reader/optional UI, without the Git adapter')
parser.add_argument('--build-dir', type=Path, default=Path('build-native'))
args = parser.parse_args()
if sys.version_info < (3, 10):
    parser.error("Python 3.10 or newer is required for the helper scripts and script tests")
root = Path(__file__).resolve().parents[1]
cmake = shutil.which('cmake')
if not cmake and os.name == 'nt':
    base = Path(os.environ.get('ProgramFiles', r'C:\Program Files')) / 'Microsoft Visual Studio/2022/Community'
    candidate = base / 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if candidate.is_file():
        cmake = str(candidate)
if not cmake:
    parser.error('CMake not found. Install CMake or add it to PATH.')
env = {key.upper() if os.name == 'nt' else key: value for key, value in os.environ.items()}
configure = [cmake, '-S', str(root), '-B', str(args.build_dir), '-DPython3_EXECUTABLE=' + sys.executable]
if os.name == 'nt':
    configure += ['-G', 'Visual Studio 17 2022', '-A', 'x64']
if args.cyui:
    configure += ['-DCYUEDIFF_BUILD_UI=ON', '-DCYUEDIFF_CYUI_SOURCE_DIR=' + str(args.cyui.resolve())]
if args.desktop:
    configure += ['-DCYUEDIFF_BUILD_DESKTOP=ON']
if args.no_git:
    configure += ['-DCYUEDIFF_BUILD_GIT=OFF']
if args.oodle_sdk:
    configure += ['-DCYUEDIFF_OODLE_SDK_DIR=' + str(args.oodle_sdk.resolve())]
subprocess.run(configure, env=env, check=True)
subprocess.run([cmake, '--build', str(args.build_dir), '--config', 'Release', '--parallel'], env=env, check=True)
ctest = str(Path(cmake).with_name('ctest.exe' if os.name == 'nt' else 'ctest'))
subprocess.run([ctest, '--test-dir', str(args.build_dir), '-C', 'Release', '--output-on-failure'], env=env, check=True)

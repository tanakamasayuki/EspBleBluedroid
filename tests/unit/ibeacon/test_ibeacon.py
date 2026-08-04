import subprocess
from pathlib import Path


def test_ibeacon_codec():
    here = Path(__file__).parent
    output = here / "output"
    output.mkdir(exist_ok=True)
    binary = output / "ibeacon_test"
    result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-funsigned-char",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(here / ".." / ".." / ".." / "src"),
            str(here / "ibeacon_test.cpp"),
            "-o",
            str(binary),
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr

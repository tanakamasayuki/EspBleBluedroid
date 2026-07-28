import subprocess
from pathlib import Path


def test_codec(tmp_path):
    repository = Path(__file__).resolve().parents[2]
    executable = tmp_path / "codec_test"
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{repository / 'src' / 'internal'}",
            str(repository / "tests" / "unit" / "codec_test.cpp"),
            str(repository / "src" / "internal" / "EspBleBluedroidCodec.cpp"),
            str(repository / "src" / "internal" / "EspBleBluedroidGattcState.cpp"),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)

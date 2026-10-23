"""Reject an APK missing a runnable native core for ARM32, ARM64 or CI."""
import struct
import sys
import zipfile

# ELF class and e_machine must agree with the ABI directory. In particular,
# a 64-bit ARM library renamed into armeabi-v7a does not fix error -113.
REQUIRED = {"armeabi-v7a": (1, 40), "arm64-v8a": (2, 183), "x86_64": (2, 62)}


def verify(path):
    with zipfile.ZipFile(path) as apk:
        for abi, (elf_class, machine) in REQUIRED.items():
            name = f"lib/{abi}/libmundox_ghz.so"
            if name not in apk.namelist():
                raise ValueError(f"Missing native ABI: {name}")
            header = apk.read(name)[:20]
            if len(header) < 20 or header[:4] != b"\x7fELF" or header[5] != 1:
                raise ValueError(f"Invalid little-endian ELF: {name}")
            elf_type, actual_machine = struct.unpack_from("<HH", header, 16)
            if header[4] != elf_class or actual_machine != machine or elf_type != 3:
                raise ValueError(f"Wrong native architecture: {name}")
            print(f"PASS {abi}: ELF{32 if elf_class == 1 else 64}, machine {machine}, shared library")


if __name__ == "__main__":
    try:
        verify(sys.argv[1])
    except (ValueError, zipfile.BadZipFile, OSError) as error:
        sys.exit(str(error))

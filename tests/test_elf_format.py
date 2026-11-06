import ctypes
import os
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from build import find_visual_studio

BASE = 0x40000000


def image():
    data = bytearray(256)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2, 3, 1, BASE + 96,
                     52, 0, 0, 52, 32, 1, 0, 0, 0)
    struct.pack_into("<IIIIIIII", data, 52, 1, 0, BASE, BASE,
                     128, 144, 5, 4096)
    return data


class ElfFormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name != "nt":
            raise unittest.SkipTest("The kernel's MSVC toolchain is Windows-only.")
        cls.temp = tempfile.TemporaryDirectory(prefix="minos-elf-test-")
        temp = Path(cls.temp.name)
        wrapper = temp / "export.c"
        wrapper.write_text(
            '#include "elf_format.h"\n'
            '__declspec(dllexport) int test_validate(const unsigned char *p, '
            'unsigned int n, unsigned int *e, unsigned int *s) '
            '{ return elf_validate(p,n,e,s); }\n'
        )
        dll = temp / "test.dll"
        devcmd = find_visual_studio()
        command = (
            f'call "{devcmd}" -arch=x64 -host_arch=x64 >nul && '
            f'cl /nologo /LD /I"{ROOT / "src"}" "{wrapper}" '
            f'"{ROOT / "src" / "elf_format.c"}" /link /out:"{dll}"'
        )
        subprocess.run(command, cwd=temp, check=True, shell=True)
        cls.dll = ctypes.CDLL(str(dll))
        cls.validate = cls.dll.test_validate
        cls.validate.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                 ctypes.POINTER(ctypes.c_uint),
                                 ctypes.POINTER(ctypes.c_uint)]
        cls.validate.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        # Windows keeps loaded DLLs open until the process exits.
        ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(cls.dll._handle))
        for attempt in range(10):
            try:
                cls.temp.cleanup()
                return
            except PermissionError:
                if attempt == 9:
                    raise
                time.sleep(0.2)

    def validate_image(self, data):
        buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        entry, size = ctypes.c_uint(), ctypes.c_uint()
        result = self.validate(buffer, len(data), ctypes.byref(entry), ctypes.byref(size))
        return result, entry.value, size.value

    def test_valid_segment_and_bss(self):
        self.assertEqual(self.validate_image(image()), (1, 96, 144))

    def test_valid_multiple_nonoverlapping_segments(self):
        data = image()
        struct.pack_into("<H", data, 44, 2)
        struct.pack_into("<IIIIIIII", data, 84, 1, 144, BASE + 144,
                         BASE + 144, 16, 32, 6, 1)
        self.assertEqual(self.validate_image(data), (1, 96, 176))

    def test_invalid_images(self):
        variants = []

        def mutate(offset, fmt, value):
            data = image()
            struct.pack_into(fmt, data, offset, value)
            variants.append((offset, value, data))

        mutate(4, "<B", 2)             # 64-bit
        mutate(5, "<B", 2)             # big-endian
        mutate(16, "<H", 3)            # shared object
        mutate(18, "<H", 62)           # x86-64
        mutate(24, "<I", BASE + 140)   # entry in BSS
        mutate(28, "<I", 0xFFFFFFF0)   # overflowing header table
        mutate(44, "<H", 33)           # excessive header count
        mutate(52, "<I", 3)            # interpreter
        mutate(52, "<I", 7)            # unsupported TLS
        mutate(56, "<I", 255)          # file segment past EOF
        mutate(60, "<I", BASE - 1)     # below image
        mutate(60, "<I", BASE + 65535) # segment past image
        mutate(68, "<I", 145)          # filesz exceeds memsz
        mutate(76, "<I", 4)            # no executable entry
        mutate(80, "<I", 3)            # non-power-of-two alignment
        for offset, value, data in variants:
            with self.subTest(offset=offset, value=value):
                self.assertEqual(self.validate_image(data)[0], 0)
        self.assertEqual(self.validate_image(image()[:51])[0], 0)

    def test_overlapping_segments_rejected(self):
        data = image()
        struct.pack_into("<H", data, 44, 2)
        struct.pack_into("<IIIIIIII", data, 84, 1, 120, BASE + 120,
                         BASE + 120, 16, 16, 6, 1)
        self.assertEqual(self.validate_image(data)[0], 0)


if __name__ == "__main__":
    unittest.main()

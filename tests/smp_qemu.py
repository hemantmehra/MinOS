"""Exercise two-core scheduling and one-core fallback with disposable disks."""

import json
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from run import find_qemu
from build import find_nasm
from tools.build_elf import build
from tools.make_disk import build_disk

STARTED = re.compile(r"process: started pid=(\d+)")
EXITED = re.compile(r"process: exiting pid=(\d+)\s+\[INFO\] process: exit status=(\d+)")


class Guest:
    def __init__(self, qemu, directory, image, data, cpu_count):
        self.log = directory / f"cpu{cpu_count}.log"
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
        listener.close()
        self.process = subprocess.Popen([
            str(qemu), "-machine", "pc", "-m", "512M", "-smp", str(cpu_count),
            "-drive", f"file={image},format=raw,if=ide",
            "-drive", f"file={data},format=raw,if=ide,index=1",
            "-display", "none", "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
            "-debugcon", f"file:{self.log}", "-no-reboot",
        ])
        self.socket = None
        try:
            deadline = time.monotonic() + 15
            while True:
                try:
                    self.socket = socket.create_connection(("127.0.0.1", port), timeout=2)
                    break
                except OSError:
                    if self.process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("QEMU monitor did not start")
                    time.sleep(0.1)
            self.socket.settimeout(10)
            self.stream = self.socket.makefile("rwb", buffering=0)
            self.stream.readline()
            self.qmp("qmp_capabilities")
            self.wait_for(lambda text: "kernel: shell started" in text, 30)
        except BaseException:
            self.close()
            raise

    def close(self):
        if hasattr(self, "stream"):
            self.stream.close()
        if self.socket is not None:
            self.socket.close()
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=10)

    def qmp(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write((json.dumps(request) + "\n").encode())
        while True:
            response = json.loads(self.stream.readline())
            if "return" in response:
                return response["return"]
            if "error" in response:
                raise RuntimeError(str(response["error"]))

    def monitor(self, command, cpu=0):
        return self.qmp("human-monitor-command",
                        {"command-line": command, "cpu-index": cpu})

    def text(self):
        return self.log.read_text(errors="replace") if self.log.exists() else ""

    def wait_for(self, predicate, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.text()
            if predicate(text):
                return text
            if self.process.poll() is not None:
                break
            time.sleep(0.1)
        raise RuntimeError("Guest timed out or exited:\n" + self.text())

    def type_command(self, command):
        keys = {" ": "spc", "&": "shift-7"}
        for character in command:
            self.monitor("sendkey " + keys.get(character, character))
            time.sleep(0.12)
        self.monitor("sendkey ret")

    def start_workload(self, command, count):
        previous = len(STARTED.findall(self.text()))
        self.type_command(command)
        text = self.wait_for(lambda output: len(STARTED.findall(output)) >= previous + count, 30)
        return [int(pid) for pid in STARTED.findall(text)[previous:previous + count]]

    def wait_exit(self, pids, expected):
        def statuses(text):
            return {int(pid): int(status) for pid, status in EXITED.findall(text)}

        text = self.wait_for(lambda output: all(pid in statuses(output) for pid in pids), 60)
        actual = statuses(text)
        for pid, status in zip(pids, expected):
            if actual[pid] != status:
                raise RuntimeError(f"pid {pid} exited {actual[pid]}, expected {status}:\n{text}")

    def assert_concurrent_workers(self):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            self.qmp("stop")
            try:
                registers = [self.monitor("info registers", cpu) for cpu in (0, 1)]
            finally:
                self.qmp("cont")
            fields = []
            for register_set in registers:
                values = {}
                for name in ("CS", "CR3", "EDI"):
                    match = re.search(r"\b" + name + r"\s*=\s*([0-9a-fA-F]+)", register_set)
                    if match:
                        values[name] = int(match.group(1), 16)
                fields.append(values)
            if (all(item.get("CS") == 0x1B and "CR3" in item for item in fields)
                    and fields[0]["CR3"] == fields[1]["CR3"]):
                raise RuntimeError("The same address space is running on both CPUs")
            if all(item.get("CS") == 0x1B and item.get("EDI") == 0x534D5057
                   for item in fields) and fields[0].get("CR3") != fields[1].get("CR3"):
                return
            time.sleep(0.03)
        raise RuntimeError("Did not observe both workers running simultaneously:\n" + self.text())


def main():
    qemu = find_qemu()
    if qemu is None:
        raise RuntimeError("QEMU was not found")
    nasm = find_nasm()
    if nasm is None:
        raise RuntimeError("NASM was not found")
    boot = ROOT / "build" / "boot" / "boot.bin"
    kernel = ROOT / "build" / "kernel" / "kernel.exe"
    if not boot.is_file() or not kernel.is_file():
        raise RuntimeError("Run python build.py --elf first")
    with tempfile.TemporaryDirectory(prefix="minos-smp-") as temporary:
        directory = Path(temporary)
        smp = directory / "SMPTEST.EXE"
        fpu = directory / "FPUTEST.EXE"
        filesystem = directory / "FSTEST.EXE"
        build([ROOT / "tests" / "smp_process.c"], smp)
        build([ROOT / "tests" / "fpu_context.c"], fpu)
        build([ROOT / "tests" / "filesystem_services.c"], filesystem)
        minc_tests = []
        for source, name in (("desktop_start.c", "DESKTEST.EXE"),
                             ("desktop_client.c", "DESKCLNT.EXE"),
                             ("http_socket.c", "HTTPTST.EXE")):
            output = directory / name
            subprocess.run([
                sys.executable, str(ROOT / "tools" / "minc.py"),
                str(ROOT / "tests" / source), "-o", str(output),
                "-I", str(ROOT / "src" / "programs" / "include"),
                "--runtime", str(ROOT / "src" / "programs" / "runtime" / "crtl.asm"),
                "--runtime", str(ROOT / "src" / "programs" / "runtime" / "minos.asm"),
                "--nasm", str(nasm),
            ], check=True)
            minc_tests.append((f"Programs/{name}", output))
        extra = [(f"Programs/{p.name}", p) for p in (ROOT / "build" / "programs").glob("*.EXE")]
        extra += [(f"Programs/{name}", path) for name, path in (
            ("SMPTEST.EXE", smp), ("SMPC1.EXE", smp), ("SMPC2.EXE", smp),
            ("SMPFAIL.EXE", smp), ("SMPLONG.EXE", smp),
            ("FPUTEST.EXE", fpu), ("FPUCHILD.EXE", fpu),
            ("FSTEST.EXE", filesystem))]
        extra += minc_tests
        for cpus in (1, 2):
            image = directory / f"system{cpus}.img"
            data = directory / f"data{cpus}.img"
            build_disk(boot, kernel, image, ROOT, extra)
            subprocess.run([sys.executable, str(ROOT / "tools" / "make_data_disk.py"),
                            "--output", str(data)], check=True)
            guest = Guest(qemu, directory, image, data, cpus)
            try:
                if cpus == 2 and "smp: started APIC ID=" not in guest.text():
                    raise RuntimeError("Second CPU did not start:\n" + guest.text())
                pids = guest.start_workload("smptest", 3)
                if cpus == 2:
                    guest.assert_concurrent_workers()
                guest.wait_exit(pids, [0, 0, 0])
                pids = guest.start_workload("fputest", 3)
                guest.wait_exit(pids, [0, 0, 0])
                if cpus == 2:
                    pids = guest.start_workload("smptest &", 3)
                    guest.assert_concurrent_workers()
                    killer = guest.start_workload("kill smpc1", 1)
                    guest.wait_exit([pids[0], pids[1], pids[2], killer[0]],
                                    [1, 255, 0, 0])
                worker = guest.start_workload("smplong &", 1)
                starts_before_kill = len(STARTED.findall(guest.text()))
                killer = guest.start_workload("kill httpsrv", 1)
                guest.wait_exit(killer, [0])
                text = guest.wait_for(
                    lambda output: "kernel: restarting HTTP server after exit" in output
                    and len(STARTED.findall(output)) >= starts_before_kill + 2, 15)
                if worker[0] in {int(pid) for pid, _ in EXITED.findall(text)}:
                    raise RuntimeError("Busy worker ended before the server restarted")
                killer = guest.start_workload("kill smplong", 1)
                guest.wait_exit([worker[0], killer[0]], [255, 0])
                pids = guest.start_workload("httpget", 1)
                guest.wait_exit(pids, [0])
                pids = guest.start_workload("httptst", 1)
                guest.wait_exit(pids, [0])
                pids = guest.start_workload("fstest", 1)
                guest.wait_exit(pids, [0])
                pids = guest.start_workload("desktest", 2)
                guest.wait_exit([pids[0]], [0])
                pids = guest.start_workload("deskclnt", 1)
                guest.wait_exit(pids, [0])
                print(f"{cpus}-CPU scheduling and x87 tests passed")
            finally:
                guest.close()


if __name__ == "__main__":
    main()

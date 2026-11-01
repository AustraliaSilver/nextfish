import subprocess
import sys

print("Testing builds/nexus-v15.exe...")
proc = subprocess.Popen(
    ["builds/nexus-v15.exe"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True
)

commands = [
    "xboard",
    "protover 2",
    "memory 64",
    "new",
    "force",
    "setboard 5rk1/pp3pbp/3pb1p1/4p2P/q2nP3/2P1BPN1/P1PQ2P1/2KR3R w - - 1 19",
    "level 40 1 0",
    "st 1",
    "go",
]

stdout_data, stderr_data = "", ""
for cmd in commands:
    proc.stdin.write(cmd + "\n")
proc.stdin.flush()

import time
time.sleep(1.5)
proc.stdin.write("quit\n")
proc.stdin.flush()

stdout_data, stderr_data = proc.communicate(timeout=5)

print("STDERR:")
print(stderr_data)

print("STDOUT:")
print(stdout_data)

assert "move " in stdout_data, "Engine did not produce a move!"
print("Smoke test PASSED!")

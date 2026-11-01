import subprocess
import time

p = subprocess.Popen(['builds/nexus-v11.exe'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

commands = [
    'xboard',
    'protover 2',
    'new',
    'sd 12',  # Fixed depth 12
    'go'
]

for cmd in commands:
    p.stdin.write(cmd + '\n')
p.stdin.flush()

time.sleep(3.0)
p.stdin.write('quit\n')
p.stdin.flush()

out, _ = p.communicate(timeout=5)
lines = [l for l in out.split('\n') if l.strip()]
print('\n'.join(lines))

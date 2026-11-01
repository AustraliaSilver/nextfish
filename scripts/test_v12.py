import subprocess
import time

p = subprocess.Popen(['builds/nexus-v12.exe'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

commands = [
    'xboard',
    'protover 2',
    'new',
    'st 1',  # 1 second search
    'go'
]

for cmd in commands:
    p.stdin.write(cmd + '\n')
p.stdin.flush()

time.sleep(1.5)
p.stdin.write('quit\n')
p.stdin.flush()

out, _ = p.communicate(timeout=3)
lines = [l for l in out.split('\n') if l.strip()]
print('\n'.join(lines))

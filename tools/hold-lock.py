import fcntl
import sys
import time

path = sys.argv[1]
seconds = float(sys.argv[2])
with open(path, 'a+b') as handle:
    fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
    sys.stdout.write('READY\n')
    sys.stdout.flush()
    time.sleep(seconds)

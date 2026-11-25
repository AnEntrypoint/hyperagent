import fcntl
import sys

path = sys.argv[1]
with open(path, 'a+b') as handle:
    try:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        sys.exit(0)
sys.exit(1)

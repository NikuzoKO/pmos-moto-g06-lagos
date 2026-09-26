import os, sys, select, termios, tty
port = sys.argv[1]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    tty.setraw(fd)
except termios.error:
    pass
for command in sys.argv[2:]:
    os.write(fd, (command + "\r").encode())
    output = b""
    while True:
        ready, _, _ = select.select([fd], [], [], 2)
        if not ready:
            break
        output += os.read(fd, 4096)
        if b"OK\r" in output or b"ERROR" in output:
            break
    print(port, repr(command), "->", output.decode(errors="replace").strip().replace("\r\n", " | "))
os.close(fd)

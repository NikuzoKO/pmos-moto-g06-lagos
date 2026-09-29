#!/usr/bin/env python3
"""Minimal logd stand-in: reads liblog datagrams from <root>/dev/socket/logdw
and appends them as text lines to a file (default stdout)."""
import os
import socket
import struct
import sys
import time

PRIORITIES = "??VDIWEF"
root = sys.argv[1]
out = open(sys.argv[2], "a", buffering=1) if len(sys.argv) > 2 else sys.stdout
path = os.path.join(root, "dev/socket/logdw")
try:
    os.unlink(path)
except FileNotFoundError:
    pass
sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
sock.bind(path)
os.chmod(path, 0o666)
# the stock clients also probe logdr/statsdw; make connect() fail fast instead of hanging
while True:
    data, _ = sock.recvfrom(65536)
    if len(data) < 12:
        continue
    log_id, tid, sec, nsec = struct.unpack_from("<BHII", data)
    payload = data[11:]
    if log_id == 2:  # events buffer: binary, skip
        continue
    prio = payload[0]
    tag, _, rest = payload[1:].partition(b"\0")
    msg = rest.rstrip(b"\0").decode(errors="replace")
    stamp = time.strftime("%H:%M:%S", time.localtime(sec)) + ".%03d" % (nsec // 1000000)
    level = PRIORITIES[prio] if prio < len(PRIORITIES) else "?"
    for line in msg.splitlines() or [""]:
        out.write(f"{stamp} {tid:5d} {level} {tag.decode(errors='replace')}: {line}\n")

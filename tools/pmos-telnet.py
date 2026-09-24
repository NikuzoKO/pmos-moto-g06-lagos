import socket, sys, time
cmd = sys.argv[1]; wait = float(sys.argv[2]) if len(sys.argv) > 2 else 3
s = socket.create_connection(("172.16.42.1", 23), timeout=5)
def rd(t):
    s.settimeout(t); out=b""
    end=time.time()+t
    while time.time()<end:
        try:
            d=s.recv(65536)
            if not d: break
            # refuse telnet option negotiation
            i=0; clean=b""
            while i<len(d):
                if d[i]==255 and i+2<len(d):
                    op,opt=d[i+1],d[i+2]
                    if op in (251,252): s.send(bytes([255,254,opt]))
                    elif op in (253,254): s.send(bytes([255,252,opt]))
                    i+=3; continue
                clean+=bytes([d[i]]); i+=1
            out+=clean
        except socket.timeout: break
    return out
rd(1.5)
s.send((cmd+"; echo __END__\n").encode())
out=b""; end=time.time()+wait+30
while b"__END__\r\n" not in out.replace(b"__END__\"",b"") and time.time()<end:
    out+=rd(wait)
    if out.count(b"__END__")>=2: break
sys.stdout.write(out.decode(errors="replace"))

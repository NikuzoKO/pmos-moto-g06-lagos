#!/usr/bin/python3 -I
"""lagos-gpsd: on-demand NMEA relay for the MediaTek GNSS (stock mnld in the camroot).

Serves the NMEA stream on a UNIX socket (geoclue's network-nmea nmea-socket, gps-share
style) and on 127.0.0.1:10110 (gpsd: `gpsd -N tcp://127.0.0.1:10110`). The engine only
runs while somebody is connected: first client brings up gps_drv_stp, the camroot, mnld
and gpsfeed (see gpsfeed.c); the last client leaving stops gpsfeed, which stops the GNSS
chip. mnld itself stays resident (idle).
Needs root. Log: journal (lagos-gps.service).
"""
import asyncio, glob, os, signal, subprocess, sys, time

HERE = os.path.dirname(os.path.realpath(__file__))
M = "/var/lib/lagos/camroot/merged"
SOCK = "/run/lagos-gps/nmea.sock"
TCP = ("127.0.0.1", 10110)
CHROOT = "/usr/sbin/chroot"
ENV = {
    "PATH": "/system/bin:/vendor/bin",
    "LD_PRELOAD": "/vendor/lib64/libpropshim.so",
    "ANDROID_ROOT": "/system", "ANDROID_DATA": "/data",
    "ANDROID_I18N_ROOT": "/apex/com.android.i18n", "ANDROID_TZDATA_ROOT": "/apex/com.android.tzdata",
}
PROPS = (
    "vendor.connsys.driver.ready=yes",
    "persist.vendor.connsys.chipid=0x6768",
    "vendor.connsys.adie.chipid=0x6631",
    "vendor.connsys.gps.adie.chipid=0x6631",
)

clients = set()
feed = None
mnld = None


def log(*a):
    print(*a, file=sys.stderr, flush=True)


def sh(*cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **kw)


def ensure_stack():
    """module, camroot, device nodes, props, mnld. Blocking; run in a thread."""
    global mnld
    if not os.path.exists("/dev/stpgps"):
        r = sh("/usr/sbin/modprobe", "gps_drv_stp")
        if r.returncode:
            raise RuntimeError("modprobe gps_drv_stp: " + r.stdout)
        for _ in range(50):
            if os.path.exists("/dev/stpgps"):
                break
            time.sleep(0.1)
    if not os.path.ismount(M):
        r = sh("sh", os.path.join(HERE, "../camera/camroot.sh"))
        if r.returncode:
            raise RuntimeError("camroot.sh: " + r.stdout[-300:])
    for n in ("stpgps", "stpgps2", "gps_emi", "fw_log_gps", "ccci_c2k_agps"):
        if os.path.exists("/dev/" + n) and not os.path.exists(f"{M}/dev/{n}"):
            sh("cp", "-a", "/dev/" + n, f"{M}/dev/{n}")
    for d in ("data/vendor/gps", "data/vendor/log/gps", "data/agps_supl"):
        os.makedirs(f"{M}/{d}", exist_ok=True)
    os.chmod(f"{M}/data/vendor/gps", 0o777)
    os.chmod(f"{M}/data/agps_supl", 0o777)
    prop = f"{M}/vendor/etc/lagos.prop"
    cur = open(prop).read().splitlines()
    if not all(p in cur[-len(PROPS):] for p in PROPS):
        with open(prop, "a") as f:
            f.write("\n".join(PROPS) + "\n")
    if mnld is None or mnld.poll() is not None:
        mnld = subprocess.Popen([CHROOT, M, "/vendor/bin/mnld"], env=ENV,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(3)
        log("mnld pid", mnld.pid)


async def start_feed():
    global feed
    await asyncio.to_thread(ensure_stack)
    feed = await asyncio.create_subprocess_exec(
        CHROOT, M, "/vendor/bin/gpsfeed", env=ENV,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
    log("gpsfeed pid", feed.pid)
    asyncio.create_task(pump(feed))


async def pump(proc):
    while True:
        line = await proc.stdout.readline()
        if not line:
            break
        for w in list(clients):
            try:
                w.write(line)
            except Exception:
                clients.discard(w)
    log("gpsfeed exited", proc.returncode)


async def stop_feed():
    global feed
    if feed and feed.returncode is None:
        feed.send_signal(signal.SIGTERM)
        try:
            await asyncio.wait_for(feed.wait(), 5)
        except asyncio.TimeoutError:
            feed.kill()
    feed = None


async def client(reader, writer):
    clients.add(writer)
    log("client +", len(clients))
    try:
        if feed is None or feed.returncode is not None:
            await start_feed()
        await reader.read()  # until the peer closes
    except Exception as e:
        log("client error:", e)
    finally:
        clients.discard(writer)
        writer.close()
        log("client -", len(clients))
        if not clients:
            await asyncio.sleep(10)  # grace: apps reconnect
            if not clients:
                await stop_feed()


async def main():
    os.makedirs(os.path.dirname(SOCK), exist_ok=True)
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    srv = await asyncio.start_unix_server(client, SOCK)
    os.chmod(SOCK, 0o666)
    tcp = await asyncio.start_server(client, *TCP)
    loop = asyncio.get_running_loop()
    stop = loop.create_future()
    for s in (signal.SIGTERM, signal.SIGINT):
        loop.add_signal_handler(s, stop.set_result, None)
    log("listening", SOCK, TCP)
    await stop
    await stop_feed()
    if mnld and mnld.poll() is None:
        mnld.terminate()


asyncio.run(main())

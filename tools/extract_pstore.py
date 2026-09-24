#!/usr/bin/env python3
# Extract the kernel ramoops console from an lagos expdb dump.
# LK's kedump copies the pstore region (0x4d010000, 0xe0000) into expdb at
# file offset 0x2740 on every abnormal reset. Zones are persistent_ram
# buffers: "DBGC" signature, u32 start, u32 size, then ring data.
import struct
import sys

PSTORE_OFFSET = 0x2740
PSTORE_SIZE = 0xE0000


def zones(region):
    offsets = []
    position = region.find(b'DBGC')
    while position >= 0:
        offsets.append(position)
        position = region.find(b'DBGC', position + 4)
    for index, offset in enumerate(offsets):
        end = offsets[index + 1] if index + 1 < len(offsets) else len(region)
        start, size = struct.unpack_from('<II', region, offset + 4)
        data = region[offset + 12:end]
        if size == 0:
            continue
        size = min(size, len(data))
        start = min(start, size)
        yield offset, data[start:size] + data[:start]


def main():
    dump = open(sys.argv[1], 'rb').read()
    region = dump[PSTORE_OFFSET:PSTORE_OFFSET + PSTORE_SIZE]
    for offset, text in zones(region):
        decoded = text.decode('utf-8', 'replace')
        if 'Linux version' in decoded or offset >= 0x1F000:
            print(f'=== console zone +{offset:#x}')
            print(decoded)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Drive the running game from the PC (debug builds, PLAN P1.10).

Talks to the game's own FTP server (DEBUG -> FTP SERVER, port 5000) with the
SITE commands of platform/3ds/source/debug_link_3ds.h. Standard library only.

    tmc-link.py [--ip IP] COMMAND [ARGS]

    wait [SECONDS]            until the game answers (after an install or boot); default 120
    state                     frame, in game, area, room, Link's position, health
    keys BUTTONS [FRAMES]     hold A+B+LEFT... (3DS names) for N logic frames, default 1
    press BUTTONS [FRAMES]    keys, then wait until those frames have passed
    touch X Y                 tap the bottom screen (320x240)
    frames N                  wait until the game has run N more logic frames
    sleep SECONDS             wait on the PC
    savestate [N]             save a new state, or over number N
    loadstate [N]             load save state N (1 = newest, the default)
    delstate N                delete save state N
    states                    the save states: number=area/room
    dump                      screen dump: debug/tmc-dump-NNNN-* (written at the next frame)
    switch NAME               cycle a DEBUG -> SWITCHES value ("FRAME SYNC")
    shot [DIR]                screenshots of both screens as PNG, default /tmp/tmc-shot
    rec [now]                 record the top screen around the next room change, HUD hide or fade start (now: no trigger)
    recfetch [DIR]            wait for the recording, write DIR/frames.png (a contact sheet) and list the frames
    peek WHAT LENGTH          hex bytes; WHAT is a hex address or a symbol of the ELF
    poke WHAT HEXBYTES        write bytes there
    mark                      a numbered marker in the SD log (flushes it)
    gpucheck                  the next frame drawn by both renderers, compared
    log [DIR]                 flush and fetch the newest SD log, default /tmp/tmc-debug
    run FILE                  one command per line ('#' comments), stops at the first error

The IP defaults to .3ds_ftp_ip in the repository (the build assistant's last).
Symbols come from build-3ds/game/tmc-3ds.elf through arm-none-eabi-nm.
"""
import ftplib
import io
import os
import shlex
import struct
import subprocess
import sys
import time
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ELF = os.path.join(REPO, 'build-3ds', 'game', 'tmc-3ds.elf')
NM = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
DATA = '/3ds/Minish Cap 3DS'
PORT = 5000


class LinkError(Exception):
    pass


class Link:
    def __init__(self, ip):
        self.ip = ip
        self.ftp = None
        self.symbols = None

    def connect(self, timeout=10):
        if self.ftp is None:
            ftp = ftplib.FTP()
            ftp.connect(self.ip, PORT, timeout=timeout)
            ftp.login()
            self.ftp = ftp
        return self.ftp

    def close(self):
        if self.ftp is not None:
            try:
                self.ftp.quit()
            except Exception:
                pass
            self.ftp = None

    def site(self, text):
        try:
            # A save or load can keep the game busy a few seconds.
            self.connect().sock.settimeout(40)
            reply = self.connect().sendcmd('SITE ' + text)
        except ftplib.error_reply as e:
            reply = str(e)
        except ftplib.all_errors as e:
            self.ftp = None
            raise LinkError(str(e))
        if not reply.startswith('2'):
            raise LinkError(reply)
        return reply[4:]

    def state(self):
        return dict(kv.split('=', 1) for kv in self.site('STATE').split())

    def frame(self):
        return int(self.state()['frame'])

    def wait_frames(self, n, timeout=60):
        target = self.frame() + n
        end = time.time() + timeout
        while self.frame() < target:
            if time.time() > end:
                raise LinkError('the game stopped advancing')
            time.sleep(0.05)

    def retrieve(self, remote, local):
        with open(local, 'wb') as f:
            self.connect().retrbinary('RETR ' + remote, f.write)

    def address(self, what):
        try:
            return int(what, 16)
        except ValueError:
            pass
        if self.symbols is None:
            out = subprocess.run([NM, '-C', ELF], capture_output=True, text=True, check=True).stdout
            self.symbols = {}
            for line in out.splitlines():
                parts = line.split()
                if len(parts) >= 3:
                    self.symbols[parts[2]] = int(parts[0], 16)
        if what not in self.symbols:
            raise LinkError('no symbol %s in %s' % (what, ELF))
        return self.symbols[what]


def bmp_to_png(data):
    """24-bit bottom-up BMP (what the game writes) to PNG bytes."""
    offset, = struct.unpack_from('<I', data, 10)
    width, height = struct.unpack_from('<ii', data, 18)
    stride = (width * 3 + 3) & ~3
    raw = bytearray()
    for y in range(height):
        row = data[offset + (height - 1 - y) * stride:][:width * 3]
        raw.append(0)
        for x in range(width):
            b, g, r = row[x * 3:x * 3 + 3]
            raw += bytes((r, g, b))

    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def frames_to_png(data, path, columns=6):
    """debug/frames.bin (SITE REC) to a contact sheet; returns the frame list."""
    magic, count, w, h = struct.unpack_from('<IIII', data, 0)
    if magic != 0x31524D46:
        raise LinkError('not a frame recording')
    size = 12 + w * h * 2
    rows = (count + columns - 1) // columns
    sheet = [bytearray(columns * (w + 2) * 3) for _ in range(rows * (h + 2))]
    lines = []
    for i in range(count):
        o = 16 + i * size
        vw, vh, hold, mode, fade_w, fade_l, frame = struct.unpack_from('<HHBBBBI', data, o)
        lines.append('%2d frame %6d view %dx%d mode %d%s%s' % (i, frame, vw, vh, mode, ' HOLD' if hold else '',
                                                              ' fade %d/%d' % (fade_w, fade_l) if fade_w else ''))
        cx, cy = (i % columns) * (w + 2), (i // columns) * (h + 2)
        for y in range(h):
            row = sheet[cy + y]
            for x in range(w):
                v, = struct.unpack_from('<H', data, o + 12 + (y * w + x) * 2)
                r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                row[(cx + x) * 3:(cx + x) * 3 + 3] = bytes((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    raw = b''.join(b'\0' + bytes(r) for r in sheet)

    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', columns * (w + 2), rows * (h + 2), 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
    return path + '\n' + '\n'.join(lines)


def run(link, argv):
    cmd, args = argv[0], argv[1:]
    if cmd == 'wait':
        end = time.time() + (float(args[0]) if args else 120)
        while True:
            try:
                link.close()
                link.connect(timeout=3)
                print(link.site('STATE'))
                return
            except (LinkError, EOFError) + ftplib.all_errors:
                if time.time() > end:
                    raise LinkError('no answer from %s:%d' % (link.ip, PORT))
                time.sleep(2)
    if cmd == 'state':
        print(link.site('STATE'))
    elif cmd in ('keys', 'press'):
        frames = int(args[1]) if len(args) > 1 else 1
        print(link.site('KEYS %s %d' % (args[0], frames)))
        if cmd == 'press':
            link.wait_frames(frames + 1)
    elif cmd == 'touch':
        print(link.site('TOUCH %d %d' % (int(args[0]), int(args[1]))))
    elif cmd == 'frames':
        link.wait_frames(int(args[0]))
    elif cmd == 'sleep':
        time.sleep(float(args[0]))
    elif cmd in ('savestate', 'loadstate', 'delstate'):
        print(link.site((cmd.upper() + ' ' + ' '.join(args)).strip()))
    elif cmd == 'states':
        print(link.site('STATES'))
    elif cmd == 'dump':
        print(link.site('DUMP'))
        link.wait_frames(3)
        buf = io.BytesIO()
        try:
            link.connect().retrbinary('RETR %s/debug/tmc-dump-last.txt' % DATA, buf.write)
            print('set %s: debug/tmc-dump-%04d-*' % (buf.getvalue().decode().strip(), int(buf.getvalue())))
        except Exception as exc:  # the number file may not have been written yet
            print('dump requested (%s)' % exc)
    elif cmd == 'switch':
        print(link.site('SWITCH ' + ' '.join(args)))
    elif cmd == 'shot':
        out = args[0] if args else '/tmp/tmc-shot'
        os.makedirs(out, exist_ok=True)
        link.site('SHOT')
        for screen in ('top', 'bottom'):
            buf = io.BytesIO()
            link.connect().retrbinary('RETR %s/debug/shot-%s.bmp' % (DATA, screen), buf.write)
            path = os.path.join(out, screen + '.png')
            with open(path, 'wb') as f:
                f.write(bmp_to_png(buf.getvalue()))
            print(path)
    elif cmd == 'rec':
        print(link.site(('REC ' + (args[0] if args else '')).strip()))  # rec now: no trigger, the next 36 frames
    elif cmd == 'recfetch':
        # waits for debug/frames.bin, writes OUTDIR/frames.png (a contact sheet) and prints each frame
        out = args[0] if args else '/tmp/tmc-rec'
        os.makedirs(out, exist_ok=True)
        end = time.time() + 60
        while True:
            names = [os.path.basename(n) for n in link.connect().nlst(DATA + '/debug')]
            if 'frames.bin' in names:
                break
            if time.time() > end:
                raise LinkError('no debug/frames.bin: was REC armed and did the room change?')
            time.sleep(0.5)
        buf = io.BytesIO()
        link.connect().retrbinary('RETR %s/debug/frames.bin' % DATA, buf.write)
        link.connect().voidcmd('DELE %s/debug/frames.bin' % DATA)
        with open(os.path.join(out, 'frames.bin'), 'wb') as f:
            f.write(buf.getvalue())
        print(frames_to_png(buf.getvalue(), os.path.join(out, 'frames.png')))
    elif cmd == 'peek':
        address, length = link.address(args[0]), int(args[1], 0)
        data = ''
        while len(data) < length * 2:
            n = min(256, length - len(data) // 2)
            data += link.site('PEEK %x %d' % (address + len(data) // 2, n))
        print(data)
    elif cmd == 'poke':
        print(link.site('POKE %x %s' % (link.address(args[0]), args[1])))
    elif cmd == 'mark':
        print(link.site('MARK'))
    elif cmd == 'gpucheck':
        print(link.site('GPUCHECK'))
    elif cmd == 'log':
        try:
            link.site('MARK')  # writes out the buffered lines
        except LinkError:
            pass
        out = args[0] if args else '/tmp/tmc-debug'
        os.makedirs(out, exist_ok=True)
        names = sorted(n for n in link.connect().nlst(DATA + '/debug') if 'tmc-log-0' in n)
        if not names:
            raise LinkError('no log on the SD card')
        name = os.path.basename(names[-1])
        path = os.path.join(out, name)
        link.retrieve('%s/debug/%s' % (DATA, name), path)
        print(path)
    elif cmd == 'run':
        with open(args[0]) as f:
            for line in f:
                line = line.split('#', 1)[0].strip()
                if line:
                    print('>', line)
                    run(link, shlex.split(line))
    else:
        raise LinkError('unknown command %s (see --help)' % cmd)


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    ip = None
    if argv[0] == '--ip':
        ip, argv = argv[1], argv[2:]
    if ip is None:
        try:
            with open(os.path.join(REPO, '.3ds_ftp_ip')) as f:
                ip = f.read().strip()
        except OSError:
            print('no --ip and no .3ds_ftp_ip', file=sys.stderr)
            return 2
    link = Link(ip)
    try:
        run(link, argv)
    except LinkError as e:
        print('tmc-link: %s' % e, file=sys.stderr)
        return 1
    finally:
        link.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())

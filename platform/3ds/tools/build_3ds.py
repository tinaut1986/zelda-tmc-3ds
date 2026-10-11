#!/usr/bin/env python3
"""Build Minish Cap 3DS and optionally send the CIA to the console over FTP.

Adapted from ../sm-3ds/tools/build_3ds.py (itself from ../mzm). Runs
non-interactively from flags, or shows an arrow-key menu when started from a
terminal with no flags. It drives platform/3ds/build.sh; the upload goes to
cias/tmc-3ds-<VERSION>.cia on the console's FTP server (ftpd or FBI); install
it from there with FBI.
"""

import argparse
import datetime
import os
import re
import shutil
import socket
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# The status glyphs below are not ASCII; do not die on a legacy console.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
IP_HISTORY_FILE = os.path.join(ROOT, ".3ds_ftp_ip")
BUILD_SH = os.path.join(ROOT, "platform", "3ds", "build.sh")
VERSION_SH = os.path.join(ROOT, "platform", "3ds", "tools", "version.sh")
BUILD_DIR = os.path.join(ROOT, "build-3ds", "game")
TITLE = "Minish Cap 3DS - build assistant"

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
CYAN = "\033[36m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
WHITE = "\033[37m"
HIDE_CURSOR = "\033[?25l"
SHOW_CURSOR = "\033[?25h"


class RawTerminal:
    """Unbuffered single-key input, on POSIX and Windows."""

    def __enter__(self):
        self.old_settings = None
        if os.name != "nt" and sys.stdin.isatty():
            import termios
            import tty
            self.fd = sys.stdin.fileno()
            self.old_settings = termios.tcgetattr(self.fd)
            # cbreak keeps \n -> \r\n output translation (no staircase effect).
            tty.setcbreak(self.fd)
        sys.stdout.write(HIDE_CURSOR)
        sys.stdout.flush()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        sys.stdout.write(SHOW_CURSOR)
        sys.stdout.flush()
        if os.name != "nt" and self.old_settings is not None:
            import termios
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self.old_settings)

    @staticmethod
    def _letter(ch):
        if ch in ("q", "Q"):
            return "QUIT"
        if ch in ("w", "W", "k", "K"):
            return "UP"
        if ch in ("s", "S", "j", "J"):
            return "DOWN"
        if ch == " ":
            return "SPACE"
        return ch

    def get_key(self):
        if os.name == "nt":
            import msvcrt
            ch = msvcrt.getch()
            if ch in (b"\x00", b"\xe0"):
                return {b"H": "UP", b"P": "DOWN", b"K": "LEFT", b"M": "RIGHT"}.get(msvcrt.getch(), "OTHER")
            if ch in (b"\r", b"\n"):
                return "ENTER"
            if ch == b"\x1b":
                return "ESC"
            if ch == b"\x03":
                raise KeyboardInterrupt
            return self._letter(ch.decode("utf-8", errors="ignore"))

        import select
        data = os.read(self.fd, 32)
        if not data:
            return "OTHER"
        if data == b"\x1b":
            # A lone ESC may be the start of a slow escape sequence.
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if not r:
                return "ESC"
            data += os.read(self.fd, 31)
        if data.startswith(b"\x1b[") or data.startswith(b"\x1bO"):
            return {b"A": "UP", b"B": "DOWN", b"C": "RIGHT", b"D": "LEFT"}.get(data[-1:], "OTHER")
        if data in (b"\r", b"\n"):
            return "ENTER"
        if data == b"\x03":
            raise KeyboardInterrupt
        return self._letter(data.decode("utf-8", errors="ignore"))


def clear_screen():
    if os.name == "nt":
        os.system("cls")
    else:
        sys.stdout.write("\033[2J\033[H")
        sys.stdout.flush()


def print_header():
    print(f"{BOLD}{CYAN}=================================================={RESET}")
    print(f"{BOLD}{WHITE} {TITLE}{RESET}")
    print(f"{BOLD}{CYAN}=================================================={RESET}\n")


HOTKEY_LABELS = {"r": "rescan"}


def interactive_select(title, options, default_index=0, subtitle=None, hotkeys=None):
    """Arrow-key menu. Returns the chosen index, or the sentinel mapped in
    `hotkeys` (letter -> value, case-insensitive) when that key is pressed."""
    current = default_index
    with RawTerminal() as term:
        while True:
            clear_screen()
            print_header()
            print(f"{BOLD}{title}{RESET}")
            if subtitle:
                print(f"{DIM}{subtitle}{RESET}")
            print()
            for i, opt in enumerate(options):
                label, desc = opt if isinstance(opt, tuple) else (opt, "")
                if i == current:
                    print(f"  {BOLD}{GREEN}➔ [X] {label}{RESET}")
                else:
                    print(f"    [ ] {label}")
                if desc:
                    print(f"        {DIM}{desc}{RESET}")

            help_line = "Arrows or W/S to move, Enter to choose"
            for letter in (hotkeys or {}):
                help_line += f", {letter.upper()} to {HOTKEY_LABELS.get(letter, 'act')}"
            help_line += ", Q to quit"
            print(f"\n{DIM}({help_line}){RESET}")

            key = term.get_key()
            if hotkeys and len(key) == 1 and key.lower() in hotkeys:
                return hotkeys[key.lower()]
            if key == "UP":
                current = (current - 1) % len(options)
            elif key == "DOWN":
                current = (current + 1) % len(options)
            elif key in ("ENTER", "SPACE"):
                return current
            elif key in ("ESC", "QUIT"):
                print(f"\n{YELLOW}Cancelled.{RESET}")
                sys.exit(0)


def prompt_text(label, default_value=""):
    clear_screen()
    print_header()
    prompt = f"{BOLD}{label}{RESET}"
    prompt += f" [{CYAN}{default_value}{RESET}]: " if default_value else ": "
    try:
        val = input(prompt).strip()
    except (KeyboardInterrupt, EOFError):
        print(f"\n{YELLOW}Cancelled.{RESET}")
        sys.exit(0)
    return val or default_value


def load_last_ip():
    try:
        with open(IP_HISTORY_FILE, "r", encoding="utf-8") as f:
            ip = f.read().strip()
            if ip:
                return ip
    except OSError:
        pass
    return "192.168.1."


def save_last_ip(ip):
    try:
        with open(IP_HISTORY_FILE, "w", encoding="utf-8") as f:
            f.write(ip.strip())
    except OSError:
        pass


def build_version(env):
    """The version build.sh will bake in (platform/3ds/tools/version.sh)."""
    try:
        proc = subprocess.run(["sh", VERSION_SH], env=env, capture_output=True, text=True)
        return proc.stdout.strip()
    except OSError:
        return ""


def cia_path(version):
    return os.path.join(BUILD_DIR, f"tmc-3ds-v{version}.cia")


def extract_errors(output_text):
    """The lines of cmake/gcc output that are actual errors."""
    errors = []
    for line in output_text.splitlines():
        clean = line.strip()
        if not clean or re.match(r"^\d+\s*\|", clean):
            continue
        low = clean.lower()
        if "warning:" in low and "error:" not in low:
            continue
        if any(m in low for m in ("error:", "error 1", "error 2", "error 127", "not found",
                                  "no such file", "undefined reference", "fatal error:")):
            if clean not in errors:
                errors.append(clean)
    return errors


def report_artifact(cia, send_ftp, ftp_host, ftp_port, remote_name):
    if os.path.isfile(cia):
        size_mb = os.path.getsize(cia) / (1024 * 1024)
        mtime = datetime.datetime.fromtimestamp(os.path.getmtime(cia)).strftime("%Y-%m-%d %H:%M:%S")
        print(f"  {BOLD}CIA:{RESET}       {os.path.basename(cia)}  ({size_mb:.2f} MB, {mtime})")
        print(f"  {BOLD}Saved at:{RESET}  {cia}")
    else:
        print(f"  {YELLOW}Warning: expected CIA not found at {cia}{RESET}")
    if send_ftp:
        print(f"  {BOLD}Sent to:{RESET}   ftp://{ftp_host}:{ftp_port}/cias/{remote_name}")


def run_step(cmd, env, verbose, what):
    """Run one build step. Returns (returncode, captured output)."""
    if verbose:
        return subprocess.run(cmd, env=env, cwd=ROOT).returncode, ""
    proc = subprocess.run(cmd, env=env, cwd=ROOT, capture_output=True, text=True)
    output = proc.stdout + "\n" + proc.stderr
    if proc.returncode != 0:
        print(f"\n{BOLD}{RED}✗ {what} failed:{RESET}")
        errs = extract_errors(output) or [l for l in output.strip().splitlines() if l.strip()][-6:]
        print(f"{DIM}--------------------------------------------------{RESET}")
        for err in errs:
            print(f"  {RED}• {err}{RESET}")
        print(f"{DIM}--------------------------------------------------{RESET}")
        print(f"{DIM}(run with -v / --verbose for the full compiler output){RESET}")
    return proc.returncode, output


# Where --install puts the CIA on the console, and the command that installs it
# (DEBUG -> FTP SERVER in a running debug build, platform/3ds/source/ftp_server_3ds.h).
INSTALL_SD_PATH = "/3ds/Minish Cap 3DS/update/dev.cia"


def run_build(debug=True, send_ftp=False, ftp_host="", ftp_port=5000, clean=False, dry_run=False, jobs=None,
              verbose=False, install=False):
    jobs = jobs or os.cpu_count() or 4

    print(f"\n{BOLD}{CYAN}=================================================={RESET}")
    print(f"{BOLD}{WHITE} Build summary{RESET}")
    print(f"{BOLD}{CYAN}=================================================={RESET}")
    print(f"  {BOLD}Mode:{RESET}   " + (f"{GREEN}debug (TMC3DS_DEBUG_TOOLS=1){RESET}" if debug else "production (TMC3DS_DEBUG_TOOLS=0)"))
    print(f"  {BOLD}Clean:{RESET}  {'yes (build-3ds/game removed)' if clean else 'no (incremental)'}")
    print(f"  {BOLD}FTP:{RESET}    " + (f"{YELLOW}yes ➔ {ftp_host}:{ftp_port}{RESET}" if send_ftp else "no (local CIA only)")
          + (f" {GREEN}+ install into the running game{RESET}" if send_ftp and install else ""))
    print(f"  {BOLD}Jobs:{RESET}   {jobs}")
    print(f"  {BOLD}Output:{RESET} {'verbose' if verbose else 'errors only (-v for everything)'}")
    print(f"{BOLD}{CYAN}=================================================={RESET}\n")

    env = os.environ.copy()
    devkitpro = env.setdefault("DEVKITPRO", "/opt/devkitpro")
    env.setdefault("DEVKITARM", os.path.join(devkitpro, "devkitARM"))
    env["TMC3DS_DEBUG_TOOLS"] = "1" if debug else "0"
    env["TMC3DS_JOBS"] = str(jobs)
    version = build_version(env)
    cia = cia_path(version)
    remote_name = f"tmc-3ds-{version}.cia"

    steps = []
    if clean:
        steps.append(("Cleaning (build-3ds/game)", ["rm", "-rf", BUILD_DIR]))
    steps.append(("Building the CIA", ["bash", BUILD_SH]))
    if send_ftp and install:
        # Upload, then "-SITE INSTALL" (the "-" sends it after the transfer): the game
        # installs the CIA over itself and relaunches into it.
        from urllib.parse import quote
        steps.append((f"Sending to {ftp_host}:{ftp_port} and installing in the running game",
                      ["curl", "--max-time", "300", "--ftp-create-dirs", "-T", cia,
                       f"ftp://{ftp_host}:{ftp_port}{quote(INSTALL_SD_PATH)}",
                       "-Q", f"-SITE INSTALL {INSTALL_SD_PATH}"]))
    elif send_ftp:
        steps.append((f"Sending to {ftp_host}:{ftp_port}",
                      ["curl", "--ftp-create-dirs", "-T", cia, f"ftp://{ftp_host}:{ftp_port}/cias/{remote_name}"]))

    for n, (label, cmd) in enumerate(steps, 1):
        print(f"{BOLD}{CYAN}[{n}/{len(steps)}]{RESET} {label}...")
        if dry_run:
            print(f"      {DIM}{' '.join(cmd)}{RESET}")
            continue
        rc, _ = run_step(cmd, env, verbose, label)
        if rc != 0:
            return rc

    if dry_run:
        print(f"\n{GREEN}[dry run] nothing was executed.{RESET}")
        return 0

    if send_ftp and install:
        print(f"\n{BOLD}{GREEN}✓ Built, installed and relaunched on {ftp_host}.{RESET}")
    elif send_ftp:
        print(f"\n{BOLD}{GREEN}✓ Built and uploaded to {ftp_host}:{ftp_port}.{RESET}")
    else:
        print(f"\n{BOLD}{GREEN}✓ Build finished.{RESET}")
    report_artifact(cia, send_ftp and not install, ftp_host, ftp_port, remote_name)
    return 0


def parse_ftp_argument(ftp_str, explicit_ip=None, default_port=5000):
    """'192.168.1.50' or '192.168.1.50:5000' -> (host, port)."""
    raw = (explicit_ip or ftp_str or "").strip()
    if not raw or raw.lower() == "true":
        return "", default_port
    if ":" in raw:
        host, port = raw.split(":", 1)
        try:
            return host.strip(), int(port.strip())
        except ValueError:
            return host.strip(), default_port
    return raw, default_port


def local_ipv4():
    """Best-effort local IPv4 (a UDP connect picks the interface, sends nothing)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


def probe_ftp(host, port, timeout=0.4):
    """`host` if it accepts a TCP connection and greets with FTP '220', else None."""
    try:
        with socket.create_connection((host, port), timeout=timeout) as c:
            c.settimeout(timeout)
            return host if c.recv(64).startswith(b"220") else None
    except OSError:
        return None


def scan_ftp_hosts(port=5000, timeout=0.4, workers=128):
    """Hosts on the local /24 answering FTP on `port`, sorted by last octet."""
    base_ip = local_ipv4()
    if not base_ip:
        last = load_last_ip()
        base_ip = last if last.count(".") == 3 and not last.endswith(".") else None
    if not base_ip:
        return []
    prefix = base_ip.rsplit(".", 1)[0]
    targets = [f"{prefix}.{i}" for i in range(1, 255)]
    with ThreadPoolExecutor(max_workers=workers) as pool:
        found = [h for h in pool.map(lambda h: probe_ftp(h, port, timeout), targets) if h]
    return sorted(found, key=lambda ip: int(ip.rsplit(".", 1)[1]))


def prompt_ftp_host_manual(default_ip, default_port):
    while True:
        host, port = parse_ftp_argument(prompt_text("3DS IP address", default_ip), default_port=default_port)
        if host and not host.endswith("."):
            save_last_ip(host)
            return host, port
        print(f"{RED}Enter a valid IP address.{RESET}")


RESCAN = "RESCAN"  # sentinel from interactive_select; never equals an index


def detect_ftp_host_interactive(default_port):
    """Scan the LAN for FTP servers and let the user pick one. The scan can be
    repeated (ftpd is often still starting on the first sweep), and manual
    entry is always available."""
    last_ip = load_last_ip()
    while True:
        clear_screen()
        print_header()
        print(f"{BOLD}Looking for a 3DS with FTP on the local network...{RESET}")
        print(f"{DIM}(port {default_port}; takes a second or two){RESET}")
        hosts = scan_ftp_hosts(port=default_port)

        if not hosts:
            idx = interactive_select("No 3DS with FTP found.", [("Enter the IP by hand", "")],
                                     subtitle="Check that ftpd or FBI is open and on the same Wi-Fi.",
                                     hotkeys={"r": RESCAN})
            if idx == RESCAN:
                continue
            return prompt_ftp_host_manual(last_ip, default_port)

        options = [(h, "last used" if h == last_ip else "") for h in hosts]
        options.append(("Enter another IP by hand", ""))
        idx = interactive_select("Found these FTP servers. Which one gets the CIA?", options,
                                 default_index=hosts.index(last_ip) if last_ip in hosts else 0,
                                 hotkeys={"r": RESCAN})
        if idx == RESCAN:
            continue
        if idx == len(hosts):
            return prompt_ftp_host_manual(last_ip, default_port)
        save_last_ip(hosts[idx])
        return hosts[idx], default_port


def main():
    parser = argparse.ArgumentParser(
        description="Build Minish Cap 3DS (CIA), optionally sending it over FTP.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""examples:
  ./build_3ds.sh                          # interactive menu
  ./build_3ds.sh --no-ftp                 # just build build-3ds/game/tmc-3ds-v<VERSION>.cia
  ./build_3ds.sh --ftp 192.168.1.55       # build and upload
  ./build_3ds.sh --ftp                    # upload to the last IP used
  ./build_3ds.sh --ftp 192.168.1.55:5000 --clean
  ./build_3ds.sh --install 192.168.1.55   # install into the running game (DEBUG -> FTP SERVER on)
  ./build_3ds.sh --mode prod --no-ftp     # what a release build is (no debug tools)
""")
    parser.add_argument("-m", "--mode", choices=["debug", "test", "prod", "production", "release"],
                        help="debug/test = DEBUG_TOOLS=1 (default), prod/production/release = DEBUG_TOOLS=0.")
    parser.add_argument("-f", "--ftp", nargs="?", const="true", default=None, metavar="IP[:PORT]",
                        help="Upload the CIA to the console's FTP server (default: last IP used).")
    parser.add_argument("--ip", metavar="IP", help="Console IP for the FTP upload.")
    parser.add_argument("-p", "--port", type=int, default=5000, help="FTP port (default 5000).")
    parser.add_argument("--install", nargs="?", const="true", default=None, metavar="IP[:PORT]",
                        help="Upload to the game's own FTP server (DEBUG -> FTP SERVER) and install: "
                             "the game updates itself and relaunches. Debug builds only.")
    parser.add_argument("--no-ftp", action="store_true", help="Build only, no menu.")
    parser.add_argument("-c", "--clean", action="store_true", help="Delete build-3ds/game first.")
    parser.add_argument("-j", "--jobs", type=int, default=None, help="Parallel build jobs.")
    parser.add_argument("-i", "--interactive", action="store_true", help="Show the menu even with flags.")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show the full compiler output.")
    parser.add_argument("--dry-run", action="store_true", help="Print the commands without running them.")
    args = parser.parse_args()

    interactive = args.interactive or (
        args.mode is None and args.ftp is None and args.ip is None and args.install is None and not args.no_ftp
        and not args.clean and sys.stdin.isatty())
    debug = args.mode in (None, "debug", "test")

    if interactive:
        mode_idx = interactive_select("Build mode?", [
            ("Debug (recommended for testing)",
             "DEBUG tab and its tools (TMC3DS_DEBUG_TOOLS=1, docs/debug-tools.md)."),
            ("Production / release", "What players get: none of the above (DEBUG_TOOLS=0)."),
        ])
        debug = mode_idx == 0
        ftp_idx = interactive_select("Send the CIA to the 3DS over FTP?", [
            ("Install into the running game",
             "The game has DEBUG -> FTP SERVER on: it installs the build and relaunches. No FBI."),
            ("Yes, find the 3DS on the network",
             "Scans the local network for FTP servers (ftpd / FBI) and lets you pick one."),
            ("Yes, enter the 3DS IP by hand",
             "The 3DS must be on the same Wi-Fi with ftpd or FBI open."),
            ("No, only build the CIA", "Leaves it in build-3ds/game/"),
        ])
        ftp_host, ftp_port = "", args.port
        install = ftp_idx == 0
        if ftp_idx in (0, 1):
            ftp_host, ftp_port = detect_ftp_host_interactive(args.port)
        elif ftp_idx == 2:
            ftp_host, ftp_port = prompt_ftp_host_manual(load_last_ip(), args.port)
        if install and not debug:
            print(f"{YELLOW}Installing into the running game needs a debug build; switching to debug.{RESET}")
            debug = True

        clean_idx = interactive_select("Clean build?", [
            ("No (incremental)", "Faster; switching the mode needs no clean either."),
            ("Yes (clean build first)", "Deletes build-3ds/game: after toolchain or CMake changes."),
        ])
        return run_build(debug=debug, send_ftp=bool(ftp_host), ftp_host=ftp_host, ftp_port=ftp_port,
                         clean=clean_idx == 1, dry_run=args.dry_run, jobs=args.jobs, verbose=args.verbose,
                         install=install and bool(ftp_host))

    install = args.install is not None
    if install and not debug:
        print(f"{RED}--install needs a debug build (the FTP server is in the DEBUG tab).{RESET}")
        return 1
    send_ftp = args.ftp is not None or args.ip is not None or install
    ftp_host, ftp_port = parse_ftp_argument(args.install if install else args.ftp, explicit_ip=args.ip,
                                            default_port=args.port)
    if send_ftp and not ftp_host:
        ftp_host = load_last_ip()
        if ftp_host.endswith("."):
            print(f"{RED}FTP upload requested but no IP given and none saved.{RESET}")
            print("Use --ftp 192.168.1.xxx or --ip 192.168.1.xxx")
            return 1
    if send_ftp:
        save_last_ip(ftp_host)

    return run_build(debug=debug, send_ftp=send_ftp, ftp_host=ftp_host, ftp_port=ftp_port, clean=args.clean,
                     dry_run=args.dry_run, jobs=args.jobs, verbose=args.verbose, install=install)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print(f"\n{YELLOW}Interrupted.{RESET}")
        sys.exit(130)

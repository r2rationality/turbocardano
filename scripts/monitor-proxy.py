#!/usr/bin/env python3
"""Check Linux host usage and TCP port 3001; email failures over verified TLS.

Run once per cron invocation. Exit 0 when healthy, 1 after sending an alert,
or 2 when the alert could not be sent.
"""

import argparse
from email.message import EmailMessage
from pathlib import Path
import smtplib
import socket
import ssl
import subprocess
import sys
import time


USAGE_LIMIT = 80
PASSWORD_FILE = Path(__file__).resolve().with_name(".smtp-password")


def cpu_sample():
    with open("/proc/stat") as stats:
        values = [int(value) for value in stats.readline().split()[1:9]]
    # Read the aggregate row's first eight counters, excluding duplicate guest
    # time. Count idle and I/O wait as idle, matching typical CPU usage tools.
    return sum(values), values[3] + values[4]


def cpu_usage():
    total_before, idle_before = cpu_sample()
    time.sleep(1)
    total_after, idle_after = cpu_sample()
    elapsed = total_after - total_before
    idle = idle_after - idle_before
    return 100 * (elapsed - idle) / elapsed


def ram_usage():
    memory = {}
    with open("/proc/meminfo") as stats:
        for line in stats:
            key, value = line.split(":", 1)
            memory[key] = int(value.split()[0])
    # MemAvailable includes reclaimable cache, not just completely free RAM.
    used = memory["MemTotal"] - memory["MemAvailable"]
    return 100 * used / memory["MemTotal"]


def usage_problem(name, percent):
    if percent >= USAGE_LIMIT:
        return [f"{name}: {percent:.1f}% used (must be below {USAGE_LIMIT}%)"]
    return []


def check_host():
    """Collect every failed check, including failures to obtain a measurement."""
    failures = []
    for name, measure in (("CPU", cpu_usage), ("RAM", ram_usage)):
        try:
            failures.extend(usage_problem(name, measure()))
        except (OSError, ValueError, KeyError, IndexError, ZeroDivisionError) as error:
            failures.append(f"{name} check failed: {error}")

    try:
        disks = subprocess.run(
            ["/bin/df", "--local", "--output=pcent,target",
             "--exclude-type=tmpfs", "--exclude-type=devtmpfs"],
            capture_output=True, text=True, check=True, timeout=10,
            env={"LC_ALL": "C"},
        )
        for line in disks.stdout.splitlines()[1:]:
            percent, mount = line.split(maxsplit=1)
            failures.extend(usage_problem(f"Disk {mount}", int(percent.rstrip("%"))))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        failures.append(f"Disk check failed: {error}")

    try:
        with socket.create_connection(("127.0.0.1", 3001), timeout=5):
            pass
    except OSError as error:
        failures.append(f"TCP connection to 127.0.0.1:3001 failed: {error}")
    return failures


def send_alert(args, failures):
    """Load the script-local password and authenticate only after TLS is active."""
    password = PASSWORD_FILE.read_text(encoding="utf-8").rstrip("\r\n")
    if not password:
        raise ValueError(f"{PASSWORD_FILE.name} is empty")

    message = EmailMessage()
    message["From"] = args.sender or args.smtp_user
    message["To"] = args.recipient
    message["Subject"] = f"Proxy alert: {socket.gethostname()}"
    message.set_content("\n".join(failures))

    context = ssl.create_default_context()
    port = args.port if args.port is not None else (587 if args.starttls else 465)
    if args.starttls:
        smtp = smtplib.SMTP(args.smtp_host, port, timeout=15)
    else:
        smtp = smtplib.SMTP_SSL(args.smtp_host, port, timeout=15, context=context)
    with smtp:
        if args.starttls:
            smtp.starttls(context=context)
        smtp.login(args.smtp_user, password)
        smtp.send_message(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recipient", help="alert recipient email address")
    parser.add_argument("smtp_host", help="SMTP server hostname")
    parser.add_argument("smtp_user", help="SMTP login username")
    parser.add_argument("--sender", help="sender email address (default: SMTP username)")
    parser.add_argument("--port", type=int, help="SMTP port (default: 465, or 587 with --starttls)")
    parser.add_argument("--starttls", action="store_true", help="require STARTTLS instead of implicit TLS")
    args = parser.parse_args()
    if args.port is not None and not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")

    failures = check_host()
    if not failures:
        return 0

    try:
        send_alert(args, failures)
    except (OSError, ValueError, smtplib.SMTPException) as error:
        print("\n".join(failures), file=sys.stderr)
        print(f"Could not send alert: {error}", file=sys.stderr)
        return 2
    return 1


if __name__ == "__main__":
    sys.exit(main())

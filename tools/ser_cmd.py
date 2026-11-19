#!/usr/bin/env python3
"""Send serial commands to the Control board and print what comes back.

Usage:  ser_cmd.py PORT 'cmd1' [wait_s] 'cmd2' [wait_s] ...
  - a quoted command is sent followed by newline
  - a bare number is a pause in seconds (output keeps being captured)
Used for before/after measurements (commands: u = CPU, m = memory, k = stacks, t = tasks).
"""
import sys, time, serial

port = sys.argv[1]
s = serial.Serial(port, 115200, timeout=0.2)
s.setDTR(False); s.setRTS(False)   # do not reset the board when opening the port

def drain(sec):
    end = time.time() + sec
    while time.time() < end:
        d = s.read(4096)
        if d:
            sys.stdout.write(d.decode('utf-8', 'replace')); sys.stdout.flush()

drain(0.3)
for a in sys.argv[2:]:
    try:
        drain(float(a))
    except ValueError:
        print(f"\n>>> {a}")
        s.write((a + "\n").encode())
        drain(0.8)

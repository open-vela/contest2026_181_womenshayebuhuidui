#!/usr/bin/env python3
"""SF32LB52 serial helper: send commands to the vela> console (temp tool)."""
import serial
import sys
import time

PORT = '/dev/ttyACM0'
BAUD = 1000000


def main():
    if len(sys.argv) < 2:
        print("usage: vela_cmd.py '<cmd>' ['<cmd>' ...]")
        sys.exit(1)

    ser = serial.Serial(PORT, BAUD, timeout=0.5)
    ser.read(4096)
    time.sleep(0.3)
    ser.read(4096)

    for cmd in sys.argv[1:]:
        ser.write((cmd + '\r\n').encode())
        out = b''
        t0 = time.time()
        while time.time() - t0 < 8:
            chunk = ser.read(512)
            if chunk:
                out += chunk
                if out.rstrip().endswith(b'vela>') or out.rstrip().endswith(b'nsh>'):
                    break
        text = out.decode(errors='replace')
        print('===== CMD: %s =====' % cmd)
        print(text)
    ser.close()


if __name__ == '__main__':
    main()

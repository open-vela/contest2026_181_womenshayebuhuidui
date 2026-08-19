#!/usr/bin/env python3
"""R76 稳定监听：只 enable，不重复 set scanmode（后者会打坏 LCPU IPC ring）。"""
import serial, sys, time
ser=serial.Serial('/dev/ttyACM0',1000000,timeout=.3)
ser.reset_input_buffer()
def run(c,sec):
    ser.write((c+'\n').encode()); end=time.time()+sec; b=b''
    while time.time()<end:
        d=ser.read(4096)
        if d: b+=d; sys.stdout.write(d.decode(errors='replace')); sys.stdout.flush()
    return b
run('\n',5)
run('bluetoothd &',7)
run('bttool',8)
run('enable',35)
print('\n[READY] enable already forces scan enable=0x03; no duplicate set scanmode\n',flush=True)
end=time.time()+300
while time.time()<end:
    d=ser.read(4096)
    if d: sys.stdout.write(d.decode(errors='replace')); sys.stdout.flush()
ser.close()

#!/usr/bin/env python3
"""PHD2 事件流转储：连接 4400，发 get_app_state 触发会话，打印 N 秒内所有事件。"""
import json, socket, sys, time

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 15
s = socket.create_connection(("127.0.0.1", 4400), timeout=5)
s.sendall(b'{"jsonrpc":"2.0","method":"get_app_state","params":[],"id":1}\n')
s.settimeout(0.5)
end = time.time() + secs
buf = b""
while time.time() < end:
    try:
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    except socket.timeout:
        continue
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        if line.strip():
            print(line.decode(errors="replace"))
s.close()

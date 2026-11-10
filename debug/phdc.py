#!/usr/bin/env python3
"""PHD2 JSON-RPC 客户端（事件服务器 4400，裸 JSON over TCP）。

用法: phdc.py METHOD [JSON_PARAMS]
例:   phdc.py get_status
      phdc.py set_connected '[true]'
      phdc.py guide '[{"pixels":0.5,"time":6,"timeout":30}, false]'
事件流会先推事件，跳过所有带 "method" 的通知，直到拿到 id 匹配的响应。
"""
import json
import socket
import sys

method = sys.argv[1]
params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else []
req = json.dumps({"jsonrpc": "2.0", "method": method, "params": params, "id": 1}) + "\n"

with socket.create_connection(("127.0.0.1", 4400), timeout=15) as s:
    s.sendall(req.encode())
    buf = b""
    while True:
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                obj = json.loads(line.decode())
            except Exception:
                print(line.decode(errors="replace"))
                sys.exit(0)
            if "Event" in obj or "method" in obj:   # 事件通知，跳过
                continue
            print(json.dumps(obj, ensure_ascii=False, indent=1))
            sys.exit(0)
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk

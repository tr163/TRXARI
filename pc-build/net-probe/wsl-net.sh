#!/bin/bash
echo "=== wsl.conf ==="; cat /etc/wsl.conf 2>/dev/null
echo "=== ip ==="; ip -4 addr show | grep inet
echo "=== resolv ==="; cat /etc/resolv.conf
echo "=== proxy port ==="
(exec 3<>/dev/tcp/127.0.0.1/7897) 2>&1 && echo "port 7897 OPEN" || echo "port 7897 CLOSED"
echo "=== curl verbose via proxy ==="
curl -sv -m 10 -x http://127.0.0.1:7897 https://github.com -o /dev/null 2>&1 | tail -15
echo "=== http (not https) via proxy ==="
curl -s -o /dev/null -w '%{http_code}\n' -m 10 -x http://127.0.0.1:7897 http://mirrors.tuna.tsinghua.edu.cn/ubuntu/dists/resolute/Release || echo FAIL

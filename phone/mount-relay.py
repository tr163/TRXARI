#!/usr/bin/env python3
"""本机中继：预连赤道仪 + 代答 LX200 的 ACK(0x06) 握手。

两个坑：
1) 192.168.4.1 的首个 SYN 经常丢，重传要 9s，而 INDI 的 TCP connect 只等 5s。
   这里在启动时就把上游连好并保持，INDI 连 127.0.0.1 永远是瞬时的。
2) INDI 的 LX200 驱动握手会发裸 0x06(ENQ) 等 0x06(ACK)，TR-Stepper 不认这个
   字节（只认 :GVP# 这类文本命令），握手必挂。这里在本地直接代答。

用法: python3 mount-relay.py [listen_port] [host] [port]
"""
import socket
import sys
import threading
import time

LPORT = int(sys.argv[1]) if len(sys.argv) > 1 else 4032
HOST = sys.argv[2] if len(sys.argv) > 2 else "192.168.4.1"
PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 4030
ACK = b"\x06"


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def connect_upstream():
    while True:
        try:
            s = socket.create_connection((HOST, PORT), timeout=15)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            s.settimeout(None)
            log(f"upstream {HOST}:{PORT} connected")
            return s
        except OSError as e:
            log(f"upstream retry: {e}")
            time.sleep(1)


def pump_client_to_mount(client, mount):
    """客户端->赤道仪；顺手把 ENQ 在本地答掉。"""
    try:
        while True:
            d = client.recv(4096)
            if not d:
                break
            if ACK in d:
                for _ in range(d.count(ACK)):
                    client.sendall(ACK)
                d = d.replace(ACK, b"")
                if not d:
                    continue
            mount.sendall(d)
    except OSError:
        pass
    finally:
        try:
            mount.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def pump_mount_to_client(mount, client):
    try:
        while True:
            d = mount.recv(4096)
            if not d:
                break
            client.sendall(d)
    except OSError:
        pass
    finally:
        try:
            client.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def main():
    standby = connect_upstream()          # 先把慢的一步做完
    ls = socket.socket()
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", LPORT))
    ls.listen(1)
    log(f"listening 127.0.0.1:{LPORT}")
    while True:
        c, _ = ls.accept()
        log("client connected")
        up = standby if standby else connect_upstream()
        standby = None
        c.settimeout(None)
        t = threading.Thread(target=pump_mount_to_client, args=(up, c), daemon=True)
        t.start()
        pump_client_to_mount(c, up)
        t.join()
        try:
            c.close()
            up.close()
        except OSError:
            pass
        log("client done")
        standby = connect_upstream()      # 为下一次预连


if __name__ == "__main__":
    main()

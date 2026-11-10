import socket, time, sys
H, P = "192.168.4.1", 4030
def rd(s, t=3.0):
    s.settimeout(t); out=b""
    while True:
        try:
            d = s.recv(1024)
            if not d: break
            out += d
            if b"#" in out: break
        except socket.timeout: break
    return out.decode(errors="replace")
t0=time.time()
s = socket.create_connection((H, P), timeout=25)
print(f"connected in {time.time()-t0:.1f}s")
for c in [":GVP#", ":GR#", ":GD#", ":GT#", ":GAT#"]:
    s.sendall(c.encode()); print(c, "->", repr(rd(s)))
r1 = (s.sendall(":GR#".encode()), rd(s))[1]
print("RA t0:", repr(r1))
time.sleep(5)
s.sendall(":GR#".encode()); r2 = rd(s)
print("RA t+5:", repr(r2), "CHANGED" if r2 != r1 else "FROZEN")
print("== pulse west 500ms ==")
s.sendall(":Mgw0500#".encode())
time.sleep(1.0)
for c in [":GR#", ":GD#", ":GA#", ":GZ#"]:
    s.sendall(c.encode()); print(c, "->", repr(rd(s)))
s.sendall(":Q#".encode()); time.sleep(0.3)
print("aborted")
s.close()

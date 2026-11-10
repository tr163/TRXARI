import socket, time
H,P="192.168.4.1",4030
def rd(s,t=3.0):
    s.settimeout(t); out=b""
    while b"#" not in out:
        try:
            d=s.recv(1024)
            if not d: break
            out+=d
        except socket.timeout: break
    return out.decode(errors="replace")
def q(s,c):
    s.sendall(c.encode()); return rd(s).strip("#")
def tsec(v):
    h,m,sec=v.split(":"); return int(h)*3600+int(m)*60+int(sec)
s=socket.create_connection((H,P),timeout=25)
ra0=q(s,":GR#"); t0=time.time()
print("RA0",ra0)
s.sendall(":Mgw10000#".encode())
print("pulse west 10000ms sent")
time.sleep(10.5)
ra1=q(s,":GR#"); dt=time.time()-t0
s.sendall(":Q#".encode())
d=tsec(ra1)-tsec(ra0)
if d<0: d+=86400
print(f"RA1 {ra1} elapsed={dt:.1f}s delta={d:.0f}s  (tracking-only≈{dt:.0f}s, expect≈{dt+5:.0f}s with 0.5x pulse)")
print("PULSE WORKS" if d > dt+3 else "PULSE NOT CONFIRMED")
s.close()

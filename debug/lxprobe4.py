import socket, time
H,P="192.168.4.1",4030
def rd(s,t=8.0):
    s.settimeout(t); out=b""
    while b"#" not in out:
        try:
            d=s.recv(1024)
            if not d: break
            out+=d
        except socket.timeout: break
    return out.decode(errors="replace").strip("#")
def oneshot(cmds):
    s=socket.create_connection((H,P),timeout=25)
    res=[]
    for c in cmds:
        s.sendall(c.encode()); res.append(rd(s))
    s.close(); return res
def sec(v):
    try:
        h,m,x=v.split(":"); return int(h)*3600+int(m)*60+int(x)
    except Exception: return None
print("init:", oneshot([":Te#", ":GAT#", ":GR#"]))
time.sleep(4)
ra1=oneshot([":GR#"])[0]; print("RA t0:",ra1)
s=socket.create_connection((H,P),timeout=25); s.sendall(":Mgw10000#".encode()); time.sleep(11); s.close()
ra2=oneshot([":GR#"])[0]; print("RA after pulse:",ra2)
a,b=sec(ra1),sec(ra2)
if a is not None and b is not None:
    d=(b-a)%86400; print(f"delta={d}s (expect ~5s for 0.5x 10s)")
    print("PULSE OK" if abs(d-5)<=2 else ("PULSE MISSING(delta=%d)"%d))
else: print("parse fail")
oneshot([":Q#"])

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
def sec(v):
    h,m,x=v.split(":"); return int(h)*3600+int(m)*60+int(x)
s=socket.create_connection((H,P),timeout=25)
print("track on:", repr(q(s,":Te#")), " GAT:", repr(q(s,":GAT#")))
time.sleep(1)
ra0=q(s,":GR#"); print("RA baseline:",ra0); time.sleep(4)
ra1=q(s,":GR#"); print("RA +4s:",ra1,"FROZEN" if ra0==ra1 else "MOVING")
print("== pulse west 10000ms ==")
s.sendall(":Mgw10000#".encode()); time.sleep(11)
ra2=q(s,":GR#")
s.sendall(":Q#".encode())
d=(sec(ra2)-sec(ra1))%86400
print("RA after pulse:",ra2,"delta=",d,"s  (0.5x*10s => ~5s)")
print("PULSE OK" if abs(d-5)<=2 else "PULSE MISSING")
s.close()

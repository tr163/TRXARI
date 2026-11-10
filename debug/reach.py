import socket
try:
    s=socket.create_connection(("192.168.4.1",4030),timeout=20)
    s.sendall(b":GVP#"); s.settimeout(5); print("reply:",s.recv(64))
    s.close()
except Exception as e:
    print("FAIL:",e)

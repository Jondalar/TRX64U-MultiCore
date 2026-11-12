import socket,struct,math,time,sys
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.bind(('',11001)); s.settimeout(3)
L=R=0.0;n=0;t0=time.time()
while time.time()-t0<float(sys.argv[1] if len(sys.argv)>1 else 1.5):
    try: d,_=s.recvfrom(2000)
    except socket.timeout: break
    v=struct.unpack('<%dh'%((len(d)-2)//2), d[2:])
    for i in range(0,len(v)-1,2): L+=v[i]*v[i]; R+=v[i+1]*v[i+1]; n+=1
print(round(math.sqrt(L/n),1) if n else 0, round(math.sqrt(R/n),1) if n else 0)

import sys,os
d=open(sys.argv[1],'rb').read()
def sec(t,s): return d[((t-1)*40+s)*256:((t-1)*40+s+1)*256]
# header/dir at track 40
s=sec(40,0); print("hdr",s[4:26])
t,sc=40,3
out=sys.argv[2]
while t:
    b=sec(t,sc)
    for i in range(8):
        e=b[i*32:(i+1)*32]
        ty=e[2]
        if ty&7:
            name=e[5:21].rstrip(b'\xa0').decode('latin1')
            ft,fs=e[3],e[4]; blocks=e[30]+256*e[31]
            data=b''
            while ft:
                x=sec(ft,fs)
                if x[0]: data+=x[2:]; ft,fs=x[0],x[1]
                else: data+=x[2:x[1]+1]; break
            print(name,ty&7,blocks,len(data),hex(data[0]+256*data[1]) if len(data)>1 else '')
            open(os.path.join(out,name.replace('/','_')),'wb').write(data)
    t,sc=b[0],b[1]

import sys,os,termios,time,select,re
fd=os.open('/dev/ttyACM0',os.O_RDWR|os.O_NOCTTY)
a=termios.tcgetattr(fd); a[0]=0;a[1]=0;a[3]=0; a[2]|=termios.CLOCAL|termios.CREAD; termios.tcsetattr(fd,termios.TCSANOW,a)
Q=b'\x1b[6n'
def rd(t,stop=None):
    o=b'';e=time.time()+t
    while time.time()<e:
        r,_,_=select.select([fd],[],[],0.2)
        if r:
            d=os.read(fd,65536)
            for _ in range(d.count(Q)): os.write(fd,b'\x1b[1;1R')
            o+=d
            if stop and stop in o: break
    return o
os.write(fd,b'\n'); rd(1.0)
cmd=sys.argv[1]; to=float(sys.argv[2]) if len(sys.argv)>2 else 15
open_=f"cat > /tmp/c.sh <<'XEOF'\n{cmd}\nXEOF\n".encode()
for i in range(0,len(open_),256):
    os.write(fd,open_[i:i+256]); time.sleep(0.05)
rd(0.5)
os.write(fd,b'sh /tmp/c.sh 2>&1; echo __E""ND__\n')
o=rd(to,b'__END__')
s=o.decode('utf8','replace').replace('\r','').replace(Q.decode(),'')
s=re.sub(r'\x1b\[[0-9;?]*[A-Za-z]','',s)
i=s.find('__E""ND__'); s=s[i+10:] if i>=0 else s
print(s.replace('__END__','<done>'))

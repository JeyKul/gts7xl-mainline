import os,sys,time,subprocess
# wait.py gone|acm|twrp seconds
m,t=sys.argv[1],float(sys.argv[2]); e=time.time()+t; s=time.time()
def adb():
    return b'recovery' in subprocess.run(['adb','devices'],capture_output=True).stdout
while time.time()<e:
    ok={'gone':not os.path.exists('/dev/ttyACM0'),'acm':os.path.exists('/dev/ttyACM0'),'twrp':adb()}[m]
    if ok: print(m,'after %.0fs'%(time.time()-s)); sys.exit(0)
    time.sleep(2)
print(m,'TIMEOUT'); sys.exit(1)

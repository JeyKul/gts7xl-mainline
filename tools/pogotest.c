#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
int main(void){
  int f=open("/dev/i2c-9",O_RDWR); if(f<0){perror("open");return 1;}
  if(ioctl(f,I2C_SLAVE_FORCE,0x2a)<0){perror("addr");return 1;}
  unsigned char req[3]={3,0,1},h[3];
  for(int i=0;i<5;i++){
    int w=write(f,req,3); int we=errno;
    int r=read(f,h,3); int re=errno;
    printf("try%d write=%d(%s) read=%d(%s) hdr=%02x %02x %02x\n",i,w,w<0?strerror(we):"ok",r,r<0?strerror(re):"ok",h[0],h[1],h[2]);
    usleep(100000);
  }
  return 0;
}

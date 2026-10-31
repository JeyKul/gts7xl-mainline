#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
int main(void){ int f=open("/dev/i2c-0",O_RDWR); if(f<0){perror("open");return 1;}
 int addrs[]={0x25,0x66,0x69}; for(int a=0;a<3;a++){ ioctl(f,I2C_SLAVE_FORCE,addrs[a]); printf("addr 0x%02x:",addrs[a]);
  for(unsigned char r=0;r<0x10;r++){ unsigned char v=0xee; if(write(f,&r,1)==1&&read(f,&v,1)==1) printf(" %02x",v); else printf(" --"); } printf("\n"); }
 return 0; }

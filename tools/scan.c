#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
int main(void){
  int f=open("/dev/i2c-9",O_RDWR); if(f<0){perror("open");return 1;}
  int hits=0; unsigned char b;
  for(int a=3;a<0x78;a++){ if(ioctl(f,I2C_SLAVE_FORCE,a)<0)continue;
    int r=read(f,&b,1); if(r==1){printf("ACK 0x%02x val=%02x\n",a,b);hits++;}
    else if(errno!=ENXIO) printf("0x%02x err=%s\n",a,strerror(errno));
  }
  printf("scan done hits=%d\n",hits); return 0;
}

/* Route the MAX77705 USB data switch to the SoC, as the stock MUIC driver does. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
int main(void){
  unsigned char cmd[3]={0x21,0x06,0x09}, end[2]={0x41,0x00};
  int f=open("/dev/i2c-0",O_RDWR); if(f<0){perror("open");return 1;}
  if(ioctl(f,I2C_SLAVE_FORCE,0x25)<0){perror("addr");return 1;}
  if(write(f,cmd,3)!=3||write(f,end,2)!=2){perror("write");return 1;}
  puts("usbroute ok"); return 0;
}

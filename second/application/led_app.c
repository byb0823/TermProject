#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>   
#include <sys/ioctl.h>

#define MODE_NONE   0
#define MODE_ALL    1
#define MODE_SINGLE 2
#define MODE_MANUAL 3
#define MODE_RESET  4

int main()
{
    int fd = open("/dev/led_device", O_RDWR);
    int cmd, lednum;

    if(fd < 0){
        printf("디바이스 파일 오픈 실패\n");
        printf("다음 명령어를 실행하세요:\n");
        printf("sudo mknod /dev/led_device c 255 0\n");
        printf("sudo chmod 666 /dev/led_device\n");
        return 0;
    }

    while(1){
        printf("\nMode 1: 1\n");
        printf("Mode 2: 2\n");
        printf("Mode 3: 3\n");
        printf("Mode 4: 4\n");
        printf("Type a mode: ");
        scanf("%d", &cmd);

        if(cmd == 1) {
            ioctl(fd, MODE_ALL);
        }
        else if(cmd == 2) {
            ioctl(fd, MODE_SINGLE);
        }
        else if(cmd == 3) {
            ioctl(fd, MODE_MANUAL);

            while(1){
                printf("LED to enable: ");
                scanf("%d", &lednum);

                if(lednum == 4){
                    ioctl(fd, MODE_RESET);
                    break;
                }

                if(lednum >= 0 && lednum <= 3){
                    ioctl(fd, 100, lednum);
                }
            }
        } 
        else if(cmd == 4){
            ioctl(fd, MODE_RESET);
        }
    }

    close(fd);
    return 0;
}
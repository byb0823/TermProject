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
    int fd = open("/dev/leddev", O_RDWR);
    int cmd, lednum;

    if(fd < 0){
        printf("디바이스 파일 오픈 실패\n");
        return 0;
    }

    while(1){
        printf("\n===== LED Control =====\n");
        printf("1: 전체 모드\n");
        printf("2: 개별 모드\n");
        printf("3: 수동 모드\n");
        printf("4: 리셋\n");
        printf("입력: ");
        scanf("%d", &cmd);

        if(cmd == 1) ioctl(fd, MODE_ALL);
        else if(cmd == 2) ioctl(fd, MODE_SINGLE);
        else if(cmd == 3) {
            ioctl(fd, MODE_MANUAL);

            while(1){
                printf("수동 모드: LED 번호 입력(0~3), 4 입력 시 리셋: ");
                scanf("%d", &lednum);

                if(lednum == 4){
                    ioctl(fd, MODE_RESET);
                    break;
                }

                if(lednum >=0 && lednum <= 3){
                    ioctl(fd, 100, &lednum);
                }
            }

        } else if(cmd == 4){
            ioctl(fd, MODE_RESET);
        }
    }

    close(fd);
    return 0;
}

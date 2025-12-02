#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_PATH "/dev/led_driver"

void display_menu(void)
{
    printf("Mode 1: 1\n");
    printf("Mode 2: 2\n");
    printf("Mode 3: 3\n");
    printf("Mode 4: 4\n");
    printf("Type a mode: ");
}

int main(void)
{
    int fd;
    char mode[10];
    int mode_num;

    printf("LED Control User Program\n");

    /* 디바이스 파일 열기 */
    fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device");
        printf("Make sure to create device node:\n");
        printf("  sudo mknod %s c <major_number> 0\n", DEVICE_PATH);
        printf("  sudo chmod 666 %s\n", DEVICE_PATH);
        return -1;
    }

    while (1) {
        display_menu();
        
        if (fgets(mode, sizeof(mode), stdin) == NULL)
            break;

        /* 입력 검증 */
        if (sscanf(mode, "%d", &mode_num) != 1 || mode_num < 0 || mode_num > 4) {
            printf("Invalid input! Please enter 0-4.\n");
            continue;
        }

        /* 종료 처리 */
        if (mode_num == 0) {
            printf("Exiting program...\n");
            break;
        }

        /* 커널 모듈에 모드 전송 */
        if (write(fd, mode, strlen(mode)) < 0) {
            perror("Write failed");
            break;
        }

        if (mode_num == 4) {
            printf("Mode RESET! All LEDs turned off.\n");
        } else {
            printf("Mode %d activated!\n", mode_num);
        }
    }

    close(fd);
    printf("Program terminated.\n");
    return 0;
}
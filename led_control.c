#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_PATH "/dev/led_driver"

void display_menu(void)
{
    printf("\n=== LED Control Menu ===\n");
    printf("Mode 1: All LEDs blink\n");
    printf("Mode 2: Single LED shift\n");
    printf("Mode 3: Manual LED control\n");
    printf("Mode 4: Reset\n");
    printf("0: Exit program\n");
    printf("Type a mode: ");
}

void display_manual_menu(void)
{
    printf("\n=== Manual LED Control ===\n");
    printf("0: Toggle LED 0\n");
    printf("1: Toggle LED 1\n");
    printf("2: Toggle LED 2\n");
    printf("3: Toggle LED 3\n");
    printf("4: Exit manual mode\n");
    printf("Select LED: ");
}

int manual_mode_control(int fd)
{
    char input[10];
    int led_num;
    
    printf("\nEntering Manual Mode...\n");
    
    while (1) {
        display_manual_menu();
        
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
        
        /* 입력 검증 */
        if (sscanf(input, "%d", &led_num) != 1 || led_num < 0 || led_num > 4) {
            printf("Invalid input! Please enter 0-4.\n");
            continue;
        }
        
        /* LED 토글 또는 종료 명령 전송 */
        if (write(fd, input, strlen(input)) < 0) {
            perror("Write failed");
            return -1;
        }
        
        if (led_num == 4) {
            printf("Exiting Manual Mode...\n");
            break;
        } else {
            printf("LED %d toggled!\n", led_num);
        }
    }
    
    return 0;
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
        } else if (mode_num == 3) {
            /* Manual 모드 진입 */
            if (manual_mode_control(fd) < 0) {
                break;
            }
        } else {
            printf("Mode %d activated!\n", mode_num);
        }
    }

    close(fd);
    printf("Program terminated.\n");
    return 0;
}
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

void display_led_menu(void)
{
    printf("Select LED (0-3) or 4 to return: ");
}

void handle_mode3(int fd)
{
    char led_input[10];
    int led_num;
    char command[10];
    
    printf("MODE_MANUAL activated!\n");
    
    while (1) {
        display_led_menu();
        
        if (fgets(led_input, sizeof(led_input), stdin) == NULL)
            break;
        
        /* 입력 검증 */
        if (sscanf(led_input, "%d", &led_num) != 1 || led_num < 0 || led_num > 4) {
            printf("Invalid input! Please enter 0-4.\n");
            continue;
        }
        
        /* 4번 입력 시 mode 선택으로 돌아가기 */
        if (led_num == 4) {
            printf("Returning to mode selection...\n");
            break;
        }
        
        /* LED 토글 명령 전송 (30-33) */
        snprintf(command, sizeof(command), "%d", 30 + led_num);
        
        if (write(fd, command, strlen(command)) < 0) {
            perror("Write failed");
            break;
        }
        
        printf("LED %d toggled!\n", led_num);
    }
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
        
        /* MODE 3 처리 */
        if (mode_num == 3) {
            /* MODE_MANUAL 활성화 */
            if (write(fd, mode, strlen(mode)) < 0) {
                perror("Write failed");
                break;
            }
            /* LED 선택 루프 진입 */
            handle_mode3(fd);
            continue;
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
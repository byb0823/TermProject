#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

// 드라이버와 동일한 명령어 정의
#define MODE_ALL     1
#define MODE_SINGLE  2
#define MODE_MANUAL  3
#define MODE_RESET   4
#define IOCTL_MANUAL_CONTROL 100 

void print_menu(void) {
    printf("\n--- LED Control Mode ---\n");
    printf("Mode 1: %d (전체 모드)\n", MODE_ALL);
    printf("Mode 2: %d (개별 모드)\n", MODE_SINGLE);
    printf("Mode 3: %d (수동 모드)\n", MODE_MANUAL);
    printf("Mode 4: %d (리셋 모드)\n", MODE_RESET);
    printf("------------------------\n");
}

void manual_mode_loop(int fd) {
    int led_input;
    char buffer[10];

    printf("\n--- Manual Mode Activated (Enter 0-4) ---\n");
    printf("Enter LED to enable/disable (0, 1, 2, 3), or 4 to RESET: \n");
    
    while (1) {
        printf("LED to enable: ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }
        
        if (sscanf(buffer, "%d", &led_input) != 1) {
            printf("Invalid input. Please enter a number.\n");
            continue;
        }

        if (led_input == 4) {
            printf("Input 4 received. Resetting mode.\n");
            break; 
        }

        if (led_input >= 0 && led_input <= 3) {
            if (ioctl(fd, IOCTL_MANUAL_CONTROL, led_input) < 0) {
                perror("ioctl failed for MANUAL_CONTROL");
                return;
            }
        } 
        else {
            printf("Ignoring input: %d (Only 0-4 are valid)\n", led_input);
        }
    }
}

int main(void)
{
    int fd;
    int mode_input;

    fd = open("/dev/led_device", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/led_device (Did you run 'mknod /dev/led_device c 255 0'?)");
        return 1;
    }

    printf("LED Device opened successfully.\n");
    
    while(1) {
        print_menu();
        printf("Type a mode: ");
        
        if (scanf("%d", &mode_input) != 1) {
            printf("Invalid mode input. Exiting.\n");
            break; 
        }
        while (getchar() != '\n'); 

        switch (mode_input) {
            case MODE_ALL:
            case MODE_SINGLE:
            case MODE_RESET:
                if (ioctl(fd, mode_input, 0) < 0) {
                    perror("ioctl failed for mode");
                }
                break;

            case MODE_MANUAL:
                if (ioctl(fd, MODE_MANUAL, 0) < 0) {
                    perror("ioctl failed for MODE_MANUAL");
                }
                manual_mode_loop(fd);
                ioctl(fd, MODE_RESET, 0); 
                break;
            
            default:
                printf("Unknown mode selected. Please choose 1, 2, 3, or 4.\n");
                break;
        }
    }

    close(fd);
    return 0;
}
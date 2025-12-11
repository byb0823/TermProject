#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/cdev.h>
#include <linux/jiffies.h> // jiffies와 HZ 사용을 위해 포함

#define DEV_NAME    "led_device"
#define DEV_MAJOR    255

#define MODE_NONE    0
#define MODE_ALL     1
#define MODE_SINGLE  2
#define MODE_MANUAL  3
#define MODE_RESET   4

#define HIGH 1
#define LOW  0

static int led[4] = {23, 24, 25, 1};

/* 모드 & 상태 */
static int current_mode = MODE_NONE;
static int led_state[4] = {0, 0, 0, 0};
static int manual_led_state[4] = {0, 0, 0, 0};
// **[수정]** 개별 모드에서 현재 켜져 있는 LED 인덱스
static int current_single_led_index = 0; 

/* 타이머 */
static struct timer_list led_timer;

/* LED 제어 함수 */
static void set_all_led(int value)
{
    int i;
    for (i = 0; i < 4; i++) {
        led_state[i] = value;
        gpio_set_value(led[i], value);
    }
}

static void toggle_all_led(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        led_state[i] = !led_state[i];
        gpio_set_value(led[i], led_state[i]);
    }
}

// **[수정]** 개별 모드: 현재 인덱스를 추적하며 하나씩 순차적으로 켜고 끄기
static void set_single_mode(void)
{
    // 1. 이전 LED 끄기
    gpio_set_value(led[current_single_led_index], LOW);
    
    // 2. 다음 LED 인덱스 계산 (0 -> 1 -> 2 -> 3 -> 0 순환)
    current_single_led_index = (current_single_led_index + 1) % 4;
    
    // 3. 현재 LED 켜기
    gpio_set_value(led[current_single_led_index], HIGH);
    
    // led_state 배열도 업데이트 (선택 사항, 필요하다면)
    int i;
    for (i = 0; i < 4; i++) {
        led_state[i] = (i == current_single_led_index) ? HIGH : LOW;
    }
    
    printk(KERN_INFO "Single Mode: LED[%d] is ON\n", current_single_led_index);
}

static void toggle_manual_led(int sw_index)
{
    manual_led_state[sw_index] = !manual_led_state[sw_index];
    gpio_set_value(led[sw_index], manual_led_state[sw_index]);
    printk(KERN_INFO "Manual LED[%d] = %d\n", sw_index, manual_led_state[sw_index]);
}

static void reset_mode(void)
{
    int i;
    
    del_timer_sync(&led_timer); // 동기화된 타이머 삭제 사용
    
    for (i = 0; i < 4; i++) {
        led_state[i] = LOW;
        manual_led_state[i] = LOW;
        gpio_set_value(led[i], LOW);
    }
    
    current_mode = MODE_NONE;
    current_single_led_index = 0; // **[추가]** 리셋 시 인덱스 초기화
    printk(KERN_INFO "Mode RESET!\n");
}

/* 타이머 콜백 */
static void timer_func(struct timer_list *t)
{
    if (current_mode == MODE_ALL) {
        toggle_all_led();
        mod_timer(&led_timer, jiffies + HZ * 2);
    } else if (current_mode == MODE_SINGLE) {
        set_single_mode();
        mod_timer(&led_timer, jiffies + HZ * 2);
    }
}

/* IOCTL 처리 */
static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int i;
    int led_num;
    
    // 모드 변경 시 기존 타이머 삭제 및 LED 초기화 (Manual 모드 제외)
    if (cmd != MODE_MANUAL && cmd != MODE_RESET && cmd != 100) {
        del_timer_sync(&led_timer);
        set_all_led(LOW); // 모든 LED 끄기
        for (i = 0; i < 4; i++) {
            manual_led_state[i] = LOW; // Manual 상태 초기화
        }
    }
    
    switch (cmd) {
    case MODE_ALL:
        set_all_led(HIGH); // 시작 시 모두 켜짐
        current_mode = MODE_ALL;
        mod_timer(&led_timer, jiffies + HZ * 2);
        printk(KERN_INFO "Mode set to ALL\n");
        break;

    case MODE_SINGLE:
        // **[수정]** 개별 모드 진입 시, 첫 번째 LED 켜고 인덱스 초기화
        current_single_led_index = 0;
        gpio_set_value(led[current_single_led_index], HIGH);
        led_state[current_single_led_index] = HIGH;
        
        current_mode = MODE_SINGLE;
        mod_timer(&led_timer, jiffies + HZ * 2);
        printk(KERN_INFO "Mode set to SINGLE. LED[0] is ON\n");
        break;

    case MODE_MANUAL:
        // Manual 모드는 LED를 켜거나 끄지 않고 상태만 저장/출력
        current_mode = MODE_MANUAL;
        printk(KERN_INFO "Mode set to MANUAL\n");
        break;

    case MODE_RESET:
        reset_mode();
        break;

    case 100: // Manual 모드에서 특정 LED 제어 (사용자 프로그램용)
        led_num = (int)arg;
        if (current_mode == MODE_MANUAL && led_num >= 0 && led_num < 4) {
            toggle_manual_led(led_num);
        } else if (current_mode == MODE_MANUAL && led_num == 4) {
            // Manual 모드에서 4 입력 시 리셋 (요구사항 2. (4) 참고)
            reset_mode();
        } else {
            printk(KERN_WARNING "Invalid LED number (%d) or not in MANUAL mode\n", led_num);
        }
        break;
        
    default:
        printk(KERN_WARNING "Invalid IOCTL command: %u\n", cmd);
        break;
    }

    return 0;
}

static int led_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int led_release(struct inode *inode, struct file *file)
{
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = led_ioctl,
    .open = led_open,
    .release = led_release
};

static int led_init(void)
{
    int registration;
    int i;
    int ret;
    
    registration = register_chrdev(DEV_MAJOR, DEV_NAME, &fops);
    if (registration < 0) {
        printk(KERN_ERR "Failed to register character device\n");
        return registration;
    }

    // 타이머 설정
    timer_setup(&led_timer, timer_func, 0);

    // GPIO 요청 및 초기화
    for (i = 0; i < 4; i++) {
        ret = gpio_request(led[i], "LED");
        if (ret < 0) {
            printk(KERN_ERR "Failed to request LED GPIO %d\n", led[i]);
            // 실패 시 이미 요청된 GPIO를 해제
            while (--i >= 0) {
                gpio_free(led[i]);
            }
            unregister_chrdev(DEV_MAJOR, DEV_NAME);
            return ret;
        }
        gpio_direction_output(led[i], LOW);
    }

    printk(KERN_INFO "LED device driver initialized\n");
    return 0;
}

static void led_exit(void)
{
    int i;
    
    del_timer_sync(&led_timer);
    
    for (i = 0; i < 4; i++) {
        gpio_set_value(led[i], LOW);
        gpio_free(led[i]);
    }
    
    unregister_chrdev(DEV_MAJOR, DEV_NAME);
    
    printk(KERN_INFO "LED device driver removed\n");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
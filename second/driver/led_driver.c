#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/cdev.h>
#include <linux/jiffies.h>
#include <linux/err.h> // IS_ERR, PTR_ERR 매크로 사용을 위해 포함

#define DEV_NAME    "led_device"
#define DEV_MAJOR    255

// IOCTL Command Codes
#define MODE_ALL     1
#define MODE_SINGLE  2
#define MODE_MANUAL  3
#define MODE_RESET   4
#define IOCTL_MANUAL_CONTROL 100 

#define HIGH 1
#define LOW  0

static int led[4] = {23, 24, 25, 1}; // LED GPIO 핀 번호

/* 상태 변수 */
static int current_mode = 0; // 0: MODE_NONE
static int led_state[4] = {0, 0, 0, 0};
static int current_single_led_index = 0; 
static struct timer_list led_timer;

/* --- LED 제어 함수 (이전과 동일) --- */

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

static void set_single_mode(void)
{
    int i;
    
    // 1. 이전 LED 끄기
    gpio_set_value(led[current_single_led_index], LOW);
    
    // 2. 다음 LED 인덱스 계산 (0 -> 1 -> 2 -> 3 -> 0 순환)
    current_single_led_index = (current_single_led_index + 1) % 4;
    
    // 3. 현재 LED 켜기
    gpio_set_value(led[current_single_led_index], HIGH);
    
    // led_state 업데이트
    for (i = 0; i < 4; i++) {
        led_state[i] = (i == current_single_led_index) ? HIGH : LOW;
    }
    
    printk(KERN_INFO "Single Mode: LED[%d] is ON (Index: %d)\n", led[current_single_led_index], current_single_led_index);
}

static void toggle_manual_led(int index)
{
    led_state[index] = !led_state[index];
    gpio_set_value(led[index], led_state[index]);
    printk(KERN_INFO "Manual Mode: LED[%d] (GPIO %d) Toggled to %d\n", index, led[index], led_state[index]);
}


static void reset_mode(void)
{
    int i;
    
    del_timer_sync(&led_timer);
    
    for (i = 0; i < 4; i++) {
        led_state[i] = LOW;
        gpio_set_value(led[i], LOW);
    }
    
    current_mode = 0;
    current_single_led_index = 0;
    printk(KERN_INFO "Mode RESET: All LEDs OFF.\n");
}


/* --- 타이머 콜백 (이전과 동일) --- */

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


static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int led_index;
    
    // 리셋 명령이 아닌 모드 전환 명령이 들어오면 타이머 삭제 및 리셋
    if (cmd != MODE_MANUAL && cmd != MODE_RESET && cmd != IOCTL_MANUAL_CONTROL) {
        reset_mode();
    }
    
    switch (cmd) {
    case MODE_ALL:
        set_all_led(HIGH); 
        current_mode = MODE_ALL;
        mod_timer(&led_timer, jiffies + HZ * 2);
        printk(KERN_INFO "Mode 1: ALL mode activated.\n");
        break;

    case MODE_SINGLE:
        int i;
        current_single_led_index = 0;
        // 모든 LED 상태 초기화 
        for (i = 0; i < 4; i++) {
            led_state[i] = (i == 0) ? HIGH : LOW;
            gpio_set_value(led[i], led_state[i]);
        }
        current_mode = MODE_SINGLE;
        mod_timer(&led_timer, jiffies + HZ * 2);
        printk(KERN_INFO "Mode 2: SINGLE mode activated. LED[0] ON.\n");
        break;

    case MODE_MANUAL:
        reset_mode(); 
        current_mode = MODE_MANUAL;
        printk(KERN_INFO "Mode 3: MANUAL mode activated.\n");
        break;

    case MODE_RESET: 
        reset_mode();
        break;

    case IOCTL_MANUAL_CONTROL:
        led_index = (int)arg;
        
        if (current_mode == MODE_MANUAL && led_index >= 0 && led_index <= 3) {
            toggle_manual_led(led_index);
        } else if (current_mode == MODE_MANUAL && led_index == 4) { 
            reset_mode();
        } else {
            printk(KERN_WARNING "Manual Mode: Invalid input %d or not in MANUAL mode.\n", led_index);
        }
        break;
        
    default:
        printk(KERN_WARNING "Invalid IOCTL command: %u\n", cmd);
        return -EINVAL;
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
    int i;
    int ret;
    
    ret = register_chrdev(DEV_MAJOR, DEV_NAME, &fops);
    if (ret < 0) {
        printk(KERN_ERR "Failed to register character device\n");
        return ret;
    }

    timer_setup(&led_timer, timer_func, 0);

    for (i = 0; i < 4; i++) {
        ret = gpio_request(led[i], "LED");
        if (ret < 0) {
            printk(KERN_ERR "Failed to request LED GPIO %d\n", led[i]);
            
            // 실패 시, 이전에 성공적으로 요청된 모든 GPIO 해제
            int j;
            for (j = 0; j < i; j++) {
                gpio_free(led[j]);
            }
            // 문자 장치 등록 해제
            unregister_chrdev(DEV_MAJOR, DEV_NAME);
            
            return ret; // 에러 코드 반환
        }
        gpio_direction_output(led[i], LOW);
    }
    
    printk(KERN_INFO "LED device driver initialized (Major: %d)\n", DEV_MAJOR);
    return 0;
}

static void led_exit(void)
{
    int i;
    
    reset_mode(); // 타이머 삭제 및 모든 LED 끄기 포함

    for (i = 0; i < 4; i++) {
        gpio_free(led[i]);
    }
    
    unregister_chrdev(DEV_MAJOR, DEV_NAME);
    
    printk(KERN_INFO "LED device driver removed\n");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
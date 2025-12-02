#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/cdev.h>

#define DEV_NAME    "led_device"
#define DEV_MAJOR   255

#define MODE_NONE   0
#define MODE_ALL    1
#define MODE_SINGLE 2
#define MODE_MANUAL 3
#define MODE_RESET  4

#define HIGH 1
#define LOW  0

static int led[4] = {23, 24, 25, 1};

/* 모드 & 상태 */
static int current_mode = MODE_NONE;
static int led_state[4] = {0, 0, 0, 0};
static int manual_led_state[4] = {0, 0, 0, 0};

/* 타이머 */
static struct timer_list led_timer;

/* LED 제어 함수 - timer_func 보다 먼저 정의 */
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
    bool found = false;

    for (i = 0; i < 4; i++) {
        if (led_state[i] == HIGH) {
            found = true;
            break;
        }
    }

    if (found) {
        led_state[i] = LOW;
        gpio_set_value(led[i], LOW);
        i = (i + 1) % 4;
        led_state[i] = HIGH;
        gpio_set_value(led[i], HIGH);
    } else {
        led_state[0] = HIGH;
        gpio_set_value(led[0], HIGH);
    }
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
    
    del_timer(&led_timer);
    
    for (i = 0; i < 4; i++) {
        led_state[i] = LOW;
        manual_led_state[i] = LOW;
        gpio_set_value(led[i], LOW);
    }
    
    current_mode = MODE_NONE;
    printk(KERN_INFO "Mode RESET!\n");
}

/* 타이머 콜백 - LED 제어 함수들 다음에 정의 */
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
    
    switch (cmd) {
    case MODE_ALL:
        del_timer(&led_timer);
        set_all_led(HIGH);
        current_mode = MODE_ALL;
        mod_timer(&led_timer, jiffies + HZ * 2);
        printk(KERN_INFO "SW0 pressed: MODE_ALL ON!\n");
        break;

    case MODE_SINGLE:
        del_timer(&led_timer);
        set_all_led(LOW);
        led_state[0] = HIGH;
        gpio_set_value(led[0], HIGH);
        current_mode = MODE_SINGLE;
        mod_timer(&led_timer, jiffies + HZ * 2);
        printk(KERN_INFO "SW1 pressed: MODE_SINGLE ON!\n");
        break;

    case MODE_MANUAL:
        del_timer(&led_timer);
        for (i = 0; i < 4; i++) {
            manual_led_state[i] = LOW;
            gpio_set_value(led[i], LOW);
        }
        current_mode = MODE_MANUAL;
        printk(KERN_INFO "SW2 pressed: MODE_MANUAL ON!\n");
        break;

    case MODE_RESET:
        reset_mode();
        break;

    case 100:
        led_num = (int)arg;
        if (current_mode == MODE_MANUAL && led_num >= 0 && led_num < 4) {
            toggle_manual_led(led_num);
        } else {
            printk(KERN_WARNING "Invalid LED number or not in MANUAL mode\n");
        }
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

    timer_setup(&led_timer, timer_func, 0);

    for (i = 0; i < 4; i++) {
        ret = gpio_request(led[i], "LED");
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/device.h>

#define DEVICE_NAME "led_driver"
#define CLASS_NAME "led_class"
#define HIGH 1
#define LOW 0

static int major_number;
static struct class *led_class = NULL;
static struct device *led_device = NULL;
static int led[4] = { 23, 24, 25, 1 };

enum { MODE_NONE, MODE_ALL, MODE_SINGLE, MODE_MANUAL };
static int current_mode = MODE_NONE;

static struct timer_list led_timer;
static int led_state[4] = {0, 0, 0, 0};

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
    for (i = 0; i < 4; i++){
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

static void toggle_led(int led_num)
{
    if (led_num >= 0 && led_num < 4) {
        led_state[led_num] = !led_state[led_num];
        gpio_set_value(led[led_num], led_state[led_num]);
        printk(KERN_INFO "LED %d toggled to %d\n", led_num, led_state[led_num]);
    }
}

static void reset_mode(void)
{
    int i;
    
    del_timer(&led_timer);
    
    for (i = 0; i < 4; i++) {
        led_state[i] = LOW;
        gpio_set_value(led[i], LOW);
    }
    
    current_mode = MODE_NONE;
    printk(KERN_INFO "Mode RESET!\n");
}

/* 타이머 콜백 */
static void led_timer_func(struct timer_list *timer)
{
    if (current_mode == MODE_ALL) {
        toggle_all_led();
        mod_timer(timer, jiffies + HZ * 2);
    } else if(current_mode == MODE_SINGLE) {
        set_single_mode();
        mod_timer(timer, jiffies + HZ * 2);
    }
}

/* 디바이스 파일 operations */
static int device_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "LED device opened\n");
    return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "LED device closed\n");
    return 0;
}

static ssize_t device_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset)
{
    char mode_input[10];
    int value;
    
    if (len > sizeof(mode_input) - 1)
        len = sizeof(mode_input) - 1;
    
    if (copy_from_user(mode_input, buffer, len))
        return -EFAULT;
    
    mode_input[len] = '\0';
    
    if (sscanf(mode_input, "%d", &value) != 1)
        return -EINVAL;
    
    printk(KERN_INFO "Received value: %d, Current mode: %d\n", value, current_mode);
    
    /* MODE_MANUAL일 때는 LED 번호로 처리 */
    if (current_mode == MODE_MANUAL) {
        if (value >= 0 && value <= 3) {
            toggle_led(value);
        } else if (value == 4) {
            /* MODE_MANUAL 종료 */
            set_all_led(LOW);
            current_mode = MODE_NONE;
            printk(KERN_INFO "MODE_MANUAL exited\n");
        } else {
            printk(KERN_WARNING "Invalid LED number: %d\n", value);
            return -EINVAL;
        }
        return len;
    }
    
    /* 일반 모드 선택 */
    switch(value) {
        case 1: /* MODE_ALL */
            del_timer(&led_timer);
            set_all_led(HIGH);
            current_mode = MODE_ALL;
            mod_timer(&led_timer, jiffies + HZ * 2);
            printk(KERN_INFO "MODE_ALL activated\n");
            break;
            
        case 2: /* MODE_SINGLE */
            del_timer(&led_timer);
            set_all_led(LOW);
            current_mode = MODE_SINGLE;
            mod_timer(&led_timer, jiffies + HZ * 2);
            printk(KERN_INFO "MODE_SINGLE activated\n");
            break;
            
        case 3: /* MODE_MANUAL */
            del_timer(&led_timer);
            set_all_led(LOW);
            current_mode = MODE_MANUAL;
            printk(KERN_INFO "MODE_MANUAL activated (select LED 0-3 to toggle, 4 to exit)\n");
            break;
            
        case 4: /* RESET */
            reset_mode();
            break;
            
        default:
            printk(KERN_WARNING "Invalid mode: %d\n", value);
            return -EINVAL;
    }
    
    return len;
}

static struct file_operations fops = {
    .open = device_open,
    .release = device_release,
    .write = device_write,
};

/* init 함수 */
static int __init led_module_init(void)
{
    int i;

    printk(KERN_INFO "LED Driver Module Init\n");

    /* LED GPIO init */
    for (i = 0; i < 4; i++) {
        if (gpio_request(led[i], "LED")) {
            printk(KERN_ERR "Failed to request GPIO %d\n", led[i]);
            return -ENODEV;
        }
        gpio_direction_output(led[i], LOW);
    }

    /* Character device 등록 */
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ERR "Failed to register device\n");
        return major_number;
    }

    printk(KERN_INFO "Device registered with major number %d\n", major_number);
    printk(KERN_INFO "Create device: mknod /dev/%s c %d 0\n", DEVICE_NAME, major_number);

    timer_setup(&led_timer, led_timer_func, 0);

    return 0;
}

/* exit 함수 */
static void __exit led_module_exit(void)
{
    int i;

    del_timer(&led_timer);

    for (i = 0; i < 4; i++)
        gpio_free(led[i]);

    unregister_chrdev(major_number, DEVICE_NAME);

    printk(KERN_INFO "LED Driver Module Exit\n");
}

module_init(led_module_init);
module_exit(led_module_exit);

MODULE_LICENSE("GPL");
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/cdev.h>

#define DEVICE_NAME "leddev"

#define MODE_NONE   0
#define MODE_ALL    1
#define MODE_SINGLE 2
#define MODE_MANUAL 3
#define MODE_RESET  4

/* LED & SW GPIO 배열 */
static int leds[4] = {23, 24, 25, 1};

/* chrdev 관련 */
static dev_t dev;
static struct cdev cdev_struct;

/* 모드 & 상태 */
static int current_mode = MODE_NONE;
static int led_state[4] = {0,0,0,0};
static int single_index = 0;

/* 타이머 */
static struct timer_list led_timer;

/* LED 전체 Set */
static void set_all_led(int value)
{
    int i;
    for(i=0;i<4;i++) {
        led_state[i] = value;
        gpio_set_value(leds[i], value);
    }
}

/* ALL 모드 토글 */
static void toggle_all_led(void)
{
    int i;
    for(i=0;i<4;i++) {
        led_state[i] = !led_state[i];
        gpio_set_value(leds[i], led_state[i]);
    }
}

/* SINGLE 모드: 하나만 켜고 나머지 OFF */
static void single_step(void)
{
    int i;
    for(i=0;i<4;i++){
        gpio_set_value(leds[i], 0);
        led_state[i] = 0;
    }
    gpio_set_value(leds[single_index], 1);
    led_state[single_index] = 1;

    single_index = (single_index + 1) % 4;
}

/* 타이머 콜백 */
static void timer_func(struct timer_list *t)
{
    if(current_mode == MODE_ALL) {
        toggle_all_led();
        mod_timer(&led_timer, jiffies + HZ*2);

    } else if(current_mode == MODE_SINGLE) {
        single_step();
        mod_timer(&led_timer, jiffies + HZ*2);
    }
}

/* IOCTL 처리 */
static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int led_num;

    switch(cmd)
    {
    case MODE_ALL:
        current_mode = MODE_ALL;
        set_all_led(0);
        mod_timer(&led_timer, jiffies + HZ*2);
        break;

    case MODE_SINGLE:
        current_mode = MODE_SINGLE;
        single_index = 0;
        single_step();
        mod_timer(&led_timer, jiffies + HZ*2);
        break;

    case MODE_MANUAL:
        current_mode = MODE_MANUAL;
        del_timer(&led_timer);
        set_all_led(0);
        break;

    case MODE_RESET:
        current_mode = MODE_NONE;
        del_timer(&led_timer);
        set_all_led(0);
        break;

    case 100: // Manual LED toggle
        if(copy_from_user(&led_num, (int __user *)arg, sizeof(int)))
            return -EFAULT;

        if(led_num >=0 && led_num < 4) {
            led_state[led_num] = !led_state[led_num];
            gpio_set_value(leds[led_num], led_state[led_num]);
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

/* 드라이버 초기화 */
static int __init led_init(void)
{
    int i;

    alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    cdev_init(&cdev_struct, &fops);
    cdev_add(&cdev_struct, dev, 1);

    for(i=0;i<4;i++){
        gpio_request(leds[i], "LED");
        gpio_direction_output(leds[i], 0);
    }

    timer_setup(&led_timer, timer_func, 0);

    printk(KERN_INFO "LED driver loaded\n");
    return 0;
}

/* 드라이버 종료 */
static void __exit led_exit(void)
{
    int i;
    del_timer(&led_timer);
    for(i=0;i<4;i++){
        gpio_free(leds[i]);
    }

    cdev_del(&cdev_struct);
    unregister_chrdev_region(dev, 1);

    printk(KERN_INFO "LED driver unloaded\n");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");

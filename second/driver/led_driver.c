#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/cdev.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/ioctl.h>

#define DEV_NAME    "led_device"
#define DEV_MAJOR    255

// IOCTL 매직 넘버
#define LED_IOCTL_MAGIC 'L'

// IOCTL Command Codes
#define MODE_ALL     _IO(LED_IOCTL_MAGIC, 1)
#define MODE_SINGLE  _IO(LED_IOCTL_MAGIC, 2)
#define MODE_MANUAL  _IO(LED_IOCTL_MAGIC, 3)
#define MODE_RESET   _IO(LED_IOCTL_MAGIC, 4)
#define IOCTL_MANUAL_CONTROL _IOW(LED_IOCTL_MAGIC, 5, int)

#define HIGH 1
#define LOW  0

static int led[4] = {23, 24, 25, 1};

static int current_mode = 0;
static int led_state[4] = {0, 0, 0, 0};

static struct timer_list led_timer;

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

    for(i = 0; i < 4; i++) {
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

static void toggle_manual_led(int index)
{
    led_state[index] = !led_state[index];
    gpio_set_value(led[index], led_state[index]);

    if (led_state[index] == HIGH) {
        printk(KERN_INFO "led[%d] ON\n", index);
    } else {
        printk(KERN_INFO "led[%d] OFF\n", index);
    }
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
}

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
    int i;
    
    // IOCTL_MANUAL_CONTROL과 MODE_RESET을 제외한 모든 명령에서 리셋
    if (cmd != IOCTL_MANUAL_CONTROL && cmd != MODE_RESET) {
        reset_mode();
    }
    
    switch (cmd) {
        case MODE_ALL:
            set_all_led(HIGH); 
            current_mode = MODE_ALL;
            mod_timer(&led_timer, jiffies + HZ * 2);
            printk(KERN_INFO "전체 모드 ON! \n");
            break;

        case MODE_SINGLE:
            set_all_led(LOW);

            led_state[0] = HIGH;
            gpio_set_value(led[0], HIGH);

            printk(KERN_INFO "개별 모드 ON!\n");
            current_mode = MODE_SINGLE;

            mod_timer(&led_timer, jiffies + HZ * 2);
            break;

        case MODE_MANUAL:
            current_mode = MODE_MANUAL;
            printk(KERN_INFO "수동 모드 ON!\n");
            break;

        case MODE_RESET: 
            reset_mode();
            printk(KERN_INFO "모드 리셋!\n");
            break;

        case IOCTL_MANUAL_CONTROL:
            led_index = (int)arg;
            
            if (led_index >= 0 && led_index <= 3) {
                toggle_manual_led(led_index);
            } else {
                printk(KERN_WARNING "Manual Control: Invalid LED index %d.\n", led_index);
                return -EINVAL;
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
    int i, ret;
    
    ret = register_chrdev(DEV_MAJOR, DEV_NAME, &fops);
    if (ret < 0) {
        printk(KERN_ERR "Failed to register character device\n");
        return ret;
    }

    timer_setup(&led_timer, timer_func, 0);

    for (i = 0; i < 4; i++) {
        gpio_request(led[i], "LED");
        gpio_direction_output(led[i], LOW);
    }
    
    printk(KERN_INFO "LED device driver initialized (Major: %d)\n", DEV_MAJOR);
    return 0;
}

static void led_exit(void)
{
    int i;
    
    reset_mode();

    for (i = 0; i < 4; i++) {
        gpio_free(led[i]);
    }
    
    unregister_chrdev(DEV_MAJOR, DEV_NAME);
    
    printk(KERN_INFO "LED device driver removed\n");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
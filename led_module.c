#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#define HIGH 1
#define LOW 0

static int led[4] = { 23, 24, 25, 1 };
static int sw[4] = { 4, 17, 27, 22 };

static int irq_sw[4];

enum { MODE_NONE, MODE_ALL, MODE_SINGLE };
static int current_mode = MODE_NONE;

static struct timer_list led_timer;
static int led_state[4] = {0, 0, 0, 0};

/* --------------------------
 *     LED 제어 함수
 * ------------------------- */
static void set_all_led(int value)
{
    int i;
    for (i = 0; i < 4; i++) {
        led_state[i] = value;
        gpio_set_value(led[i], value);
    }
}

static void set_all_mode(void)
{
    int i;
    for (i = 0; i < 4; i++){
        led_state[i] = (led_state[i] + 1) % 2;
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

/* --------------------------
 *     타이머 콜백
 * ------------------------- */
static void led_timer_func(struct timer_list *timer)
{
    printk(KERN_INFO "timer callback function!\n");

    if (current_mode == MODE_ALL) {
        timer->expires = jiffies + HZ * 2;
        add_timer(timer);

        set_all_mode();
        return;
    }

    if(current_mode == MODE_SINGLE) {
        timer->expires = jiffies + HZ * 2;
        add_timer(timer);

        set_single_mode();
        return;
    }
}

/* --------------------------
 *      인터럽트 핸들러
 * ------------------------- */
static irqreturn_t sw_irq_handler(int irq, void *dev_id)
{
    printk(KERN_INFO "Debug %d\n",irq);
    set_all_led(LOW);

    /* SW[0] 눌림인지 확인 */
    if (irq == irq_sw[0]) {
        printk(KERN_INFO "SW0 pressed: MODE_ALL ON!\n");
        current_mode = MODE_ALL;

        /* 타이머 시작 */
        led_timer.expires = jiffies + HZ * 2;
        add_timer(&led_timer);
    }

    /* SW[1] 눌림인지 확인 */
    if(irq == irq_sw[1]){
        printk(KERN_INFO "SW1 pressed: MODE_SINGLE ON!\n");
        current_mode = MODE_SINGLE;

        led_timer.expires = jiffies + HZ * 2;
        add_timer(&led_timer);
    }

    return IRQ_HANDLED;
}

/* --------------------------
 *        init 함수
 * ------------------------- */
static int led_sw_module_init(void)
{
    int i, ret;

    printk(KERN_INFO "Module init: LED & SW control\n");

    /* LED GPIO init */
    for (i = 0; i < 4; i++) {
        gpio_request(led[i], "LED");
        gpio_direction_output(led[i], LOW);
    }

    /* SW GPIO init + IRQ 등록 */
    for (i = 0; i < 4; i++) {
        gpio_request(sw[i], "SW");
        gpio_direction_input(sw[i]);

        irq_sw[i] = gpio_to_irq(sw[i]);

        ret = request_irq(
            irq_sw[i],
            sw_irq_handler,
            IRQF_TRIGGER_RISING,
            "SW_IRQ",
            &sw[i]
        );

        if (ret < 0)
            printk(KERN_ERR "request_irq failed for SW%d\n", i);
    }

    timer_setup(&led_timer, led_timer_func, 0);

    return 0;
}

/* --------------------------
 *        exit 함수
 * ------------------------- */
static void led_sw_module_exit(void)
{
    int i;

    /* 타이머 제거 */
    del_timer(&led_timer);

    /* IRQ free */
    for (i = 0; i < 4; i++)
        free_irq(irq_sw[i], (void *)(sw_irq_handler));

    /* GPIO free */
    for (i = 0; i < 4; i++) {
        gpio_free(led[i]);
        gpio_free(sw[i]);
    }

    printk(KERN_INFO "Module exit\n");
}

module_init(led_sw_module_init);
module_exit(led_sw_module_exit);

MODULE_LICENSE("GPL");
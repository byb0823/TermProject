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

enum { MODE_NONE, MODE_ALL, MODE_SINGLE, MODE_MANUAL };
static int current_mode = MODE_NONE;

static struct timer_list led_timer;
static int led_state[4] = {0, 0, 0, 0};
static int manual_led_state[4] = {0, 0, 0, 0};

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

static void toggle_manual_led(int sw_index)
{
    manual_led_state[sw_index] = !manual_led_state[sw_index];
    gpio_set_value(led[sw_index], manual_led_state[sw_index]);

    if (manual_led_state[sw_index] == HIGH) {
        printk(KERN_INFO "led[%d] ON\n", sw_index);
    } else {
        printk(KERN_INFO "led[%d] OFF\n", sw_index);
    }
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

/* 인터럽트 핸들러 */
static irqreturn_t sw_irq_handler(int irq, void *dev_id)
{
    int i;
    
    /* SW[3] - 리셋 모드 (항상 동작) */
    if (irq == irq_sw[3]) {
        reset_mode();
        printk(KERN_INFO "SW3 pressed: 모드 리셋!\n");
        return IRQ_HANDLED;
    }
    
    /* 수동 모드일 때: SW[0], SW[1], SW[2]는 LED 토글 */
    if (current_mode == MODE_MANUAL) {
        for (i = 0; i < 3; i++) {
            if (irq == irq_sw[i]) {
                toggle_manual_led(i);
                return IRQ_HANDLED;
            }
        }
        return IRQ_HANDLED;
    }
    
    /* 수동 모드가 아닐 때: 모드 전환 */
    
    /* SW[0] - 전체 모드 */
    if (irq == irq_sw[0]) {
        del_timer(&led_timer);
        set_all_led(HIGH);
        
        printk(KERN_INFO "SW0 pressed: 전체 모드 ON!\n");
        current_mode = MODE_ALL;
        
        mod_timer(&led_timer, jiffies + HZ * 2);
        return IRQ_HANDLED;
    }

    /* SW[1] - 개별 모드 */
    if(irq == irq_sw[1]){
        del_timer(&led_timer);
        set_all_led(LOW);

        led_state[0] = HIGH;
        gpio_set_value(led[0], HIGH);
        
        printk(KERN_INFO "SW1 pressed: 개별 모드 ON!\n");
        current_mode = MODE_SINGLE;

        mod_timer(&led_timer, jiffies + HZ * 2);
        return IRQ_HANDLED;
    }
    
    /* SW[2] - 수동 모드 진입 */
    if(irq == irq_sw[2]){
        del_timer(&led_timer);
        
        /* 수동 모드 진입 시 manual_led_state 초기화 (LED는 꺼진 상태로 시작) */
        for (i = 0; i < 4; i++) {
            manual_led_state[i] = LOW;
            gpio_set_value(led[i], LOW);
        }
        
        current_mode = MODE_MANUAL;
        printk(KERN_INFO "SW2 pressed: 수동 모드 ON!\n");
        return IRQ_HANDLED;
    }

    return IRQ_HANDLED;
}

/* init 함수 */
static int led_sw_module_init(void)
{
    int i, ret;

    printk(KERN_INFO "Module init\n");

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
            NULL
        );

        if (ret < 0)
            printk(KERN_ERR "request_irq failed for SW%d\n", i);
    }

    timer_setup(&led_timer, led_timer_func, 0);

    return 0;
}

/* exit 함수 */
static void led_sw_module_exit(void)
{
    int i;

    del_timer(&led_timer);

    for (i = 0; i < 4; i++)
        free_irq(irq_sw[i], NULL);

    for (i = 0; i < 4; i++) {
        gpio_free(led[i]);
        gpio_free(sw[i]);
    }

    printk(KERN_INFO "Module exit\n");
}

module_init(led_sw_module_init);
module_exit(led_sw_module_exit);

MODULE_LICENSE("GPL");
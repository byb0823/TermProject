#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/jiffies.h>



// GPIO 번호 정의
#define PIR_GPIO 7 
static int LED[] = {23, 24, 25, 1};
static int SW[] = {4, 17, 27, 22};

// 상태 변수
static int alarm_active = 0;
static int led_state = 0;
static struct timer_list alarm_timer;
static int pir_irq;

// LED 제어 함수
static void set_all_leds(int state)
{
    int i;
    for (i = 0; i < 4; i++) {
        gpio_set_value(LED[i], state);
    }
}

// 타이머 콜백 함수 - LED 깜박임
static void alarm_timer_callback(struct timer_list *t)
{
    if (alarm_active) {
        led_state = !led_state;
        set_all_leds(led_state);
        
        // 2초 후 다시 타이머 설정
        mod_timer(&alarm_timer, jiffies + HZ * 2);
    }
}

// 스위치 인터럽트 핸들러 - 알람 끄기
static irqreturn_t switch_irq_handler(int irq, void *dev_id)
{
    if (alarm_active) {
        printk(KERN_INFO "PIR Alarm: Switch pressed, deactivating alarm\n");
        alarm_active = 0;
        del_timer(&alarm_timer);
        set_all_leds(0);  // 모든 LED 끄기
    }
    return IRQ_HANDLED;
}

// PIR 센서 인터럽트 핸들러 - 물체 감지
static irqreturn_t pir_irq_handler(int irq, void *dev_id)
{
    int pir_value = gpio_get_value(PIR_GPIO);
    
    if (pir_value == 1 && !alarm_active) {
        printk(KERN_INFO "PIR Alarm: Motion detected! Activating alarm\n");
        alarm_active = 1;
        led_state = 1;
        set_all_leds(led_state);
        
        // 타이머 시작
        mod_timer(&alarm_timer, jiffies + HZ * 2);
    }
    
    return IRQ_HANDLED;
}

static int __init pir_alarm_init(void)
{
    int ret, i;
    
    printk(KERN_INFO "PIR Alarm: Initializing module\n");
    
    // LED GPIO 설정
    for (i = 0; i < 4; i++) {
        ret = gpio_request(LED[i], "LED");
        if (ret) {
            printk(KERN_ERR "PIR Alarm: Failed to request LED GPIO %d\n", LED[i]);
            goto fail_led;
        }
        gpio_direction_output(LED[i], 0);
    }
    
    // 스위치 GPIO 설정
    for (i = 0; i < 4; i++) {
        ret = gpio_request(SW[i], "Switch");
        if (ret) {
            printk(KERN_ERR "PIR Alarm: Failed to request Switch GPIO %d\n", SW[i]);
            goto fail_sw;
        }
        gpio_direction_input(SW[i]);
        
        // 스위치 인터럽트 설정 (falling edge)
        ret = request_irq(gpio_to_irq(SW[i]), switch_irq_handler,
                         IRQF_TRIGGER_FALLING, "switch_irq", NULL);
        if (ret) {
            printk(KERN_ERR "PIR Alarm: Failed to request IRQ for switch %d\n", SW[i]);
            gpio_free(SW[i]);
            goto fail_sw;
        }
    }
    
    // PIR 센서 GPIO 설정
    ret = gpio_request(PIR_GPIO, "PIR Sensor");
    if (ret) {
        printk(KERN_ERR "PIR Alarm: Failed to request PIR GPIO\n");
        goto fail_pir;
    }
    gpio_direction_input(PIR_GPIO);
    
    // PIR 센서 인터럽트 설정 (rising edge)
    pir_irq = gpio_to_irq(PIR_GPIO);
    ret = request_irq(pir_irq, pir_irq_handler,
                     IRQF_TRIGGER_RISING, "pir_irq", NULL);
    if (ret) {
        printk(KERN_ERR "PIR Alarm: Failed to request PIR IRQ\n");
        gpio_free(PIR_GPIO);
        goto fail_pir;
    }
    
    // 타이머 초기화
    timer_setup(&alarm_timer, alarm_timer_callback, 0);
    
    printk(KERN_INFO "PIR Alarm: Module initialized successfully\n");
    return 0;

fail_pir:
    for (i = 0; i < 4; i++) {
        free_irq(gpio_to_irq(SW[i]), NULL);
    }
fail_sw:
    while (--i >= 0) {
        gpio_free(SW[i]);
    }
    i = 4;
fail_led:
    while (--i >= 0) {
        gpio_free(LED[i]);
    }
    return ret;
}

static void __exit pir_alarm_exit(void)
{
    int i;
    
    printk(KERN_INFO "PIR Alarm: Cleaning up module\n");
    
    // 알람 비활성화 및 타이머 삭제
    alarm_active = 0;
    del_timer_sync(&alarm_timer);
    
    // PIR 센서 정리
    free_irq(pir_irq, NULL);
    gpio_free(PIR_GPIO);
    
    // 스위치 정리
    for (i = 0; i < 4; i++) {
        free_irq(gpio_to_irq(SW[i]), NULL);
        gpio_free(SW[i]);
    }
    
    // LED 정리
    set_all_leds(0);
    for (i = 0; i < 4; i++) {
        gpio_free(LED[i]);
    }
    
    printk(KERN_INFO "PIR Alarm: Module removed\n");
}

module_init(pir_alarm_init);
module_exit(pir_alarm_exit);
MODULE_LICENSE("GPL");
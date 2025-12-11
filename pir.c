#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/types.h>

// --- GPIO 핀 번호 선언 및 초기화 (요청 사항 반영) ---
static int led_gpios[] = {23, 24, 25, 1}; // LED 핀 번호 직접 대입
static int sw_gpios[]  = {4, 17, 27, 22}; // SW 핀 번호 직접 대입
static int pir_gpio    = 6;              // PIR 센서 핀 (예시)

// 배열 크기 및 알람 주기 정의
#define LED_COUNT       ARRAY_SIZE(led_gpios)
#define SW_COUNT        ARRAY_SIZE(sw_gpios)
#define ALARM_PERIOD_MS 2000 // 2초 주기

// 전역 상태 변수
static struct timer_list led_timer;
static bool alarm_active = false;
static bool led_state = false; // 현재 LED 켜짐/꺼짐 상태 (true=On, false=Off)

// --- LED 제어 함수 ---
static void set_all_leds(bool state) {
    int i;
    for (i = 0; i < LED_COUNT; i++) {
        gpio_set_value(led_gpios[i], state);
    }
    led_state = state;
}

// --- LED 깜빡임 타이머 핸들러 ---
static void led_timer_handler(struct timer_list *t) {
    if (alarm_active) {
        // 현재 상태의 반대로 설정 (켜짐 -> 꺼짐, 꺼짐 -> 켜짐)
        set_all_leds(!led_state);
        
        // 2초 후 타이머 재설정
        mod_timer(&led_timer, jiffies + msecs_to_jiffies(ALARM_PERIOD_MS));
    }
}

// --- PIR 센서 인터럽트 핸들러 (알람 활성화) ---
static irqreturn_t pir_irq_handler(int irq, void *dev_id) {
    // PIR 센서가 High (움직임 감지) 신호를 냈고, 현재 알람이 비활성화 상태인 경우
    if (!alarm_active && gpio_get_value(pir_gpio) == 1) { 
        pr_info("PIR: Motion detected. Activating alarm (2 sec blinking).\n");
        alarm_active = true;
        
        // LED를 켜는 것부터 시작
        set_all_leds(true);
        
        // 2초 후 첫 번째 깜빡임을 위한 타이머 설정
        mod_timer(&led_timer, jiffies + msecs_to_jiffies(ALARM_PERIOD_MS));
    }
    return IRQ_HANDLED;
}

// --- 스위치 인터럽트 핸들러 (알람 해제) ---
static irqreturn_t sw_irq_handler(int irq, void *dev_id) {
    // 간단한 디바운스
    mdelay(20); 

    if (alarm_active) {
        pr_info("SW: Alarm reset triggered. Deactivating alarm.\n");
        
        // 1. 타이머 중지
        del_timer_sync(&led_timer);
        
        // 2. 모든 LED 비활성화
        set_all_leds(false);
        
        // 3. 알람 상태 해제
        alarm_active = false;
    }
    return IRQ_HANDLED;
}

// --- 드라이버 초기화 함수 ---
static int __init pir_alarm_init(void) {
    int ret, i;
    int sw_irqs[SW_COUNT];
    int pir_irq;

    pr_info("PIR Alarm Driver: Initializing (Task 3)...\n");

    // 1. GPIO 요청 및 설정
    // LED (출력, 초기 상태 Off)
    ret = gpio_request_array(led_gpios, LED_COUNT, "led_gpios");
    if (ret) { pr_err("Failed to request LED GPIOs\n"); return ret; }
    for (i = 0; i < LED_COUNT; i++) {
        gpio_direction_output(led_gpios[i], 0);
    }

    // SW (입력)
    ret = gpio_request_array(sw_gpios, SW_COUNT, "sw_gpios");
    if (ret) { pr_err("Failed to request SW GPIOs\n"); goto err_led; }
    // PIR (입력)
    ret = gpio_request_one(pir_gpio, GPIOF_IN, "pir_gpio");
    if (ret) { pr_err("Failed to request PIR GPIO\n"); goto err_sw; }

    // 2. 커널 타이머 설정
    timer_setup(&led_timer, led_timer_handler, 0);

    // 3. 인터럽트 설정
    // PIR 센서: 상승 엣지 (움직임 감지 시)
    pir_irq = gpio_to_irq(pir_gpio);
    ret = request_irq(pir_irq, pir_irq_handler, IRQF_TRIGGER_RISING, "pir_alarm_pir_irq", (void *)&pir_gpio);
    if (ret) { pr_err("Failed to request PIR IRQ\n"); goto err_pir; }

    // 스위치: 하강 엣지 (버튼 눌림 시)
    for (i = 0; i < SW_COUNT; i++) {
        sw_irqs[i] = gpio_to_irq(sw_gpios[i]);
        ret = request_irq(sw_irqs[i], sw_irq_handler, IRQF_TRIGGER_FALLING, "pir_alarm_sw_irq", (void *)&sw_gpios[i]);
        if (ret) { pr_err("Failed to request SW[%d] IRQ\n", i); goto err_irq; }
    }

    pr_info("PIR Alarm Driver: Ready. (PIR:%d, SWs:%d~%d, LEDs:%d~%d)\n", 
            pir_gpio, sw_gpios[0], sw_gpios[SW_COUNT-1], led_gpios[0], led_gpios[LED_COUNT-1]);
    return 0;

err_irq:
    // IRQ 해제 (에러 복구)
    for (int j = 0; j < i; j++) {
        free_irq(sw_irqs[j], (void *)&sw_gpios[j]);
    }
    free_irq(pir_irq, (void *)&pir_gpio);
err_pir:
    gpio_free(pir_gpio);
err_sw:
    gpio_free_array(sw_gpios, SW_COUNT);
err_led:
    gpio_free_array(led_gpios, LED_COUNT);
    return ret;
}

// --- 드라이버 해제 함수 ---
static void __exit pir_alarm_exit(void) {
    int i;
    int pir_irq = gpio_to_irq(pir_gpio);

    // 1. 타이머 제거 및 LED 비활성화
    del_timer_sync(&led_timer);
    set_all_leds(false);

    // 2. IRQ 해제
    free_irq(pir_irq, (void *)&pir_gpio);
    for (i = 0; i < SW_COUNT; i++) {
        free_irq(gpio_to_irq(sw_gpios[i]), (void *)&sw_gpios[i]);
    }

    // 3. GPIO 해제
    gpio_free(pir_gpio);
    gpio_free_array(sw_gpios, SW_COUNT);
    gpio_free_array(led_gpios, LED_COUNT);

    pr_info("PIR Alarm Driver: Exited.\n");
}

module_init(pir_alarm_init);
module_exit(pir_alarm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Embedded System Term Project 3 - PIR Alarm Driver (Refactored)");
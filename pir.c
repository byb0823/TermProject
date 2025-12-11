// driver_pir_alarm.h (또는 드라이버 파일 상단)

#define LED_PINS {23, 24, 25, 1}
#define SW_PINS  {4, 17, 27, 22}
#define PIR_PIN  6  // PIR 센서 핀은 임의로 6번으로 가정

#define LED_COUNT 4
#define SW_COUNT 4

// 알람 반복 주기 (2초)
#define ALARM_PERIOD_MS 2000

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>


// 전역 변수
static int led_gpios[] = LED_PINS;
static int sw_gpios[] = SW_PINS;
static int pir_gpio = PIR_PIN;

static struct timer_list led_timer;
static bool alarm_active = false;
static bool led_state = false; // 현재 LED 켜짐/꺼짐 상태 (true=On, false=Off)

// --- 2.1. LED 제어 함수 ---
static void set_all_leds(bool state) {
    int i;
    for (i = 0; i < LED_COUNT; i++) {
        gpio_set_value(led_gpios[i], state);
    }
    led_state = state; // 상태 업데이트
}

// --- 2.2. LED 깜빡임 타이머 핸들러 ---
// 전체 LED가 2초 간격으로 꺼짐과 켜짐을 반복 
static void led_timer_handler(struct timer_list *t) {
    if (alarm_active) {
        // 현재 상태의 반대로 설정
        set_all_leds(!led_state);
        
        // 2초 후 타이머 재설정
        mod_timer(&led_timer, jiffies + msecs_to_jiffies(ALARM_PERIOD_MS));
    }
}

// --- 2.3. PIR 센서 인터럽트 핸들러 ---
// 물체 접근 시 알람 활성화 [cite: 138, 148]
static irqreturn_t pir_irq_handler(int irq, void *dev_id) {
    if (!alarm_active && gpio_get_value(pir_gpio) == 1) { // 감지 (High)
        pr_info("PIR: Motion detected. Activating alarm.\n");
        alarm_active = true;
        
        // LED를 켜는 것부터 시작
        set_all_leds(true);
        
        // 2초 후 첫 번째 깜빡임을 위한 타이머 설정
        mod_timer(&led_timer, jiffies + msecs_to_jiffies(ALARM_PERIOD_MS));
    }
    return IRQ_HANDLED;
}

// --- 2.4. 스위치 인터럽트 핸들러 ---
// 알람이 켜진 상태에서 임의의 SW를 누를 경우 알람이 꺼짐 
static irqreturn_t sw_irq_handler(int irq, void *dev_id) {
    // 디바운스 처리 (간단하게 짧은 지연)
    mdelay(20); 

    if (alarm_active) {
        pr_info("SW: Alarm reset triggered. Deactivating alarm.\n");
        
        // 1. 타이머 중지
        del_timer_sync(&led_timer);
        
        // 2. 모든 LED 비활성화 [cite: 147]
        set_all_leds(false);
        
        // 3. 알람 상태 해제
        alarm_active = false;
    }
    // 스위치가 눌렸는지 확인하는 추가 로직은 생략함 (엣지 트리거 인터럽트 가정)
    return IRQ_HANDLED;
}

// --- 2.5. 드라이버 초기화 및 해제 ---

static int __init pir_alarm_init(void) {
    int ret, i;
    int sw_irqs[SW_COUNT];
    int pir_irq;

    pr_info("PIR Alarm Driver: Initializing...\n");

    // 1. GPIO 요청 및 설정
    // LED (출력)
    ret = gpio_request_array(led_gpios, LED_COUNT, "led_gpios");
    if (ret) { /* error handling */ return ret; }
    for (i = 0; i < LED_COUNT; i++) {
        gpio_direction_output(led_gpios[i], 0); // 초기 상태: Off 
    }

    // SW (입력)
    ret = gpio_request_array(sw_gpios, SW_COUNT, "sw_gpios");
    if (ret) { /* error handling */ goto err_led; }
    // PIR (입력)
    ret = gpio_request_one(pir_gpio, GPIOF_IN, "pir_gpio");
    if (ret) { /* error handling */ goto err_sw; }

    // 2. 커널 타이머 설정
    timer_setup(&led_timer, led_timer_handler, 0);

    // 3. 인터럽트 설정
    // PIR 센서 (상승 엣지 인터럽트)
    pir_irq = gpio_to_irq(pir_gpio);
    ret = request_irq(pir_irq, pir_irq_handler, IRQF_TRIGGER_RISING | IRQF_SHARED, "pir_alarm_pir_irq", (void *)pir_gpios);
    if (ret) { /* error handling */ goto err_pir; }

    // 스위치 (하강 엣지 인터럽트)
    for (i = 0; i < SW_COUNT; i++) {
        sw_irqs[i] = gpio_to_irq(sw_gpios[i]);
        ret = request_irq(sw_irqs[i], sw_irq_handler, IRQF_TRIGGER_FALLING, "pir_alarm_sw_irq", (void *)&sw_gpios[i]);
        if (ret) { /* error handling */ goto err_irq; }
    }

    pr_info("PIR Alarm Driver: Initialization complete.\n");
    return 0;

err_irq:
    // 요청된 IRQ 해제 (에러 발생 시)
    for (i = 0; i < SW_COUNT; i++) {
        if (sw_irqs[i] > 0) free_irq(sw_irqs[i], (void *)&sw_gpios[i]);
    }
    free_irq(pir_irq, (void *)pir_gpios);
err_pir:
    gpio_free(pir_gpio);
err_sw:
    gpio_free_array(sw_gpios, SW_COUNT);
err_led:
    gpio_free_array(led_gpios, LED_COUNT);
    return ret;
}

static void __exit pir_alarm_exit(void) {
    int i;
    int pir_irq = gpio_to_irq(pir_gpio);

    // 1. 타이머 제거 및 LED 비활성화
    del_timer_sync(&led_timer);
    set_all_leds(false);

    // 2. IRQ 해제
    free_irq(pir_irq, (void *)pir_gpios);
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

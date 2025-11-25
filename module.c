#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/time.h>

// GPIO 핀 정의
static int led_pins[] = {23, 24, 25, 1};
static int sw_pins[] = {4, 17, 27, 22};

// IRQ 번호 저장
static int sw_irq[4];

// LED 상태 저장 (0: OFF, 1: ON)
static int led_state[4] = {0, 0, 0, 0};

// 현재 모드 (0: 없음, 1: 전체, 2: 개별, 3: 수동, 4: 리셋)
static int current_mode = 0;

// 이전 스위치 눌림 시간 (debouncing용)
static unsigned long last_interrupt_time[4] = {0};

#define DEBOUNCE_TIME 200  // 200ms debounce

// 모든 LED 끄기
static void turn_off_all_leds(void) {
    int i;
    for (i = 0; i < 4; i++) {
        gpio_set_value(led_pins[i], 0);
        led_state[i] = 0;
    }
}

// 수동 모드: 개별 LED 토글
static void manual_mode_toggle_led(int sw_num) {
    if (sw_num >= 0 && sw_num < 3) {  // SW[0], SW[1], SW[2]만 수동 동작
        led_state[sw_num] = !led_state[sw_num];
        gpio_set_value(led_pins[sw_num], led_state[sw_num]);
        printk(KERN_INFO "Manual Mode: LED[%d] %s\n", 
               sw_num, led_state[sw_num] ? "ON" : "OFF");
    }
}

// 리셋 모드: 모든 동작 중지 및 모드 해제
static void reset_mode(void) {
    turn_off_all_leds();
    current_mode = 0;
    printk(KERN_INFO "Reset Mode: All LEDs OFF, Mode cleared\n");
}

// 스위치 인터럽트 핸들러
static irqreturn_t sw_interrupt_handler(int irq, void *dev_id) {
    int sw_num = *(int *)dev_id;
    unsigned long current_time = jiffies;
    unsigned long time_diff = jiffies_to_msecs(current_time - last_interrupt_time[sw_num]);
    
    // Debouncing: 200ms 이내 재입력 무시
    if (time_diff < DEBOUNCE_TIME) {
        return IRQ_HANDLED;
    }
    
    last_interrupt_time[sw_num] = current_time;
    
    // SW[3] - 리셋 모드
    if (sw_num == 3) {
        reset_mode();
        return IRQ_HANDLED;
    }
    
    // SW[0] - 전체 모드 선택
    if (sw_num == 0) {
        current_mode = 1;
        printk(KERN_INFO "Mode changed to: All Mode (SW[0])\n");
        return IRQ_HANDLED;
    }
    
    // SW[1] - 개별 모드 선택
    if (sw_num == 1) {
        current_mode = 2;
        printk(KERN_INFO "Mode changed to: Individual Mode (SW[1])\n");
        return IRQ_HANDLED;
    }
    
    // SW[2] - 수동 모드 선택 및 동작
    if (sw_num == 2) {
        if (current_mode != 3) {        //수동 모드가 아닌 경우 모드 변환, 수동 모드가 맞는 경우 led 켜는 동작
            current_mode = 3;
            turn_off_all_leds();
            printk(KERN_INFO "Mode changed to: Manual Mode (SW[2])\n");
        } else {
            // 수동 모드에서 SW[2] 재입력시 LED[2] 토글
            manual_mode_toggle_led(2);
        }
        return IRQ_HANDLED;
    }
    
    // 수동 모드일 때 SW[0], SW[1] 입력 처리
    if (current_mode == 3) {
        manual_mode_toggle_led(sw_num);
    }
    
    return IRQ_HANDLED;
}

static int *sw_id[4];

// 모듈 초기화
static int led_control_init(void) {
    int i, ret;
    
    printk(KERN_INFO "LED Control Module Loading...\n");
    
    // LED GPIO 초기화
    for (i = 0; i < 4; i++) {
        ret = gpio_request(led_pins[i], "led");
        if (ret < 0) {
            printk(KERN_ERR "Failed to request LED GPIO %d\n", led_pins[i]);
            goto err_led;
        }
        gpio_direction_output(led_pins[i], 0);
    }
    
    // 스위치 GPIO 및 인터럽트 초기화
    for (i = 0; i < 4; i++) {
        ret = gpio_request(sw_pins[i], "switch");
        if (ret < 0) {
            printk(KERN_ERR "Failed to request Switch GPIO %d\n", sw_pins[i]);
            goto err_sw;
        }
        
        gpio_direction_input(sw_pins[i]);
        
        sw_id[i] = kmalloc(sizeof(int), GFP_KERNEL);
        *sw_id[i] = i;
        
        sw_irq[i] = gpio_to_irq(sw_pins[i]);
        ret = request_irq(sw_irq[i], sw_interrupt_handler,
                         IRQF_TRIGGER_RISING, "switch_handler", sw_id[i]);
        if (ret < 0) {
            printk(KERN_ERR "Failed to request IRQ for SW[%d]\n", i);
            kfree(sw_id[i]);
            goto err_irq;
        }
    }
    
    printk(KERN_INFO "LED Control Module Loaded Successfully\n");
    return 0;

err_irq:
    for (i = i - 1; i >= 0; i--) {
        free_irq(sw_irq[i], sw_id[i]);
        kfree(sw_id[i]);
    }
    i = 4;
err_sw:
    for (i = i - 1; i >= 0; i--) {
        gpio_free(sw_pins[i]);
    }
    i = 4;
err_led:
    for (i = i - 1; i >= 0; i--) {
        gpio_free(led_pins[i]);
    }
    return ret;
}

// 모듈 종료
static void led_control_exit(void) {
    int i;
    
    // 모든 LED 끄기
    turn_off_all_leds();
    
    // 인터럽트 해제
    for (i = 0; i < 4; i++) {
        free_irq(sw_irq[i], sw_id[i]);
        kfree(sw_id[i]);
    }
    
    // GPIO 해제
    for (i = 0; i < 4; i++) {
        gpio_free(led_pins[i]);
        gpio_free(sw_pins[i]);
    }
    
    printk(KERN_INFO "LED Control Module Unloaded\n");
}

MODULE_LICENSE("GPL");
module_init(led_control_init);
module_exit(led_control_exit);


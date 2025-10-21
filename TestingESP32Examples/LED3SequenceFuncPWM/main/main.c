#include <stdio.h>              // Standard C I/O library, needed for printf() debugging output
#include <freertos/FreeRTOS.h>  // Core FreeRTOS definitions (tasks, queues, delays, etc.)
#include <freertos/task.h>      // Task management functions like xTaskCreate(), vTaskDelay()
#include <driver/ledc.h>        // ESP-IDF LEDC (LED Controller) driver for PWM generation
#include "sdkconfig.h"          // Auto-generated project configuration (menuconfig options)

// Shared PWM timer configuration
#define LED_PWM_TIMER             LEDC_TIMER_0
#define LED_PWM_MODE              LEDC_LOW_SPEED_MODE
#define LED_PWM_DUTY_RESOLUTION   LEDC_TIMER_10_BIT
#define LED_PWM_FREQUENCY_HZ      1000  // 1 kHz avoids flicker, efficient for LED dimming

// LED GPIO assignments
#define LED1_GPIO_PIN             5
#define LED2_GPIO_PIN             4
#define LED3_GPIO_PIN             23

// LED PWM channels
#define LED1_PWM_CHANNEL          LEDC_CHANNEL_0
#define LED2_PWM_CHANNEL          LEDC_CHANNEL_1
#define LED3_PWM_CHANNEL          LEDC_CHANNEL_2

// Fade parameters
#define FADE_STEP                 10
#define FADE_DELAY_MS             20

// Modified fade_led: fades in and out once, then returns
/*- ledc_channel_t
→ This is an enumeration type that defines valid LEDC channels (e.g., LEDC_CHANNEL_0, LEDC_CHANNEL_1, etc.).
→ You use it to tell the driver which PWM channel to assign to a GPIO pin.
*/
void fade_led_once(int gpio_pin, ledc_channel_t channel)
{
    // Configure PWM channel for this LED
/*- ledc_channel_config_t
→ This is a struct type that holds configuration parameters for a PWM channel.
→ You fill it out with fields like .gpio_num, .duty, .timer_sel, etc., and pass it to ledc_channel_config().
*/
    ledc_channel_config_t led_channel = {
        .gpio_num   = gpio_pin,
        .speed_mode = LED_PWM_MODE,
        .channel    = channel,
        .timer_sel  = LED_PWM_TIMER,
        .duty       = 0
    };
    ledc_channel_config(&led_channel);

    // Fade in
    for (int duty = 0; duty <= 1023; duty += FADE_STEP) {
        ledc_set_duty(LED_PWM_MODE, channel, duty);
        ledc_update_duty(LED_PWM_MODE, channel);
        vTaskDelay(FADE_DELAY_MS / portTICK_PERIOD_MS);
    }

    // Fade out
    for (int duty = 1023; duty >= 0; duty -= FADE_STEP) {
        ledc_set_duty(LED_PWM_MODE, channel, duty);
        ledc_update_duty(LED_PWM_MODE, channel);
        vTaskDelay(FADE_DELAY_MS / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    // Configure shared PWM timer
    ledc_timer_config_t led_pwm_timer = {
        .speed_mode      = LED_PWM_MODE,
        .duty_resolution = LED_PWM_DUTY_RESOLUTION,
        .timer_num       = LED_PWM_TIMER,
        .freq_hz         = LED_PWM_FREQUENCY_HZ
    };
    ledc_timer_config(&led_pwm_timer);

    // Sequential fade loop
    while (true) {
        fade_led_once(LED1_GPIO_PIN, LED1_PWM_CHANNEL);
        fade_led_once(LED2_GPIO_PIN, LED2_PWM_CHANNEL);
        fade_led_once(LED3_GPIO_PIN, LED3_PWM_CHANNEL);
    }
}
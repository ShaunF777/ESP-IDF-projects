#include <stdio.h>              // C library will be used for the printf function
#include <freertos/FreeRTOS.h>  // Provides the core FreeRTOS types and functions
#include <freertos/task.h>      // Allows to use of the 'vTaskDelay' non-blocking delay function
#include <driver/ledc.h>        // Includes the functions required to configure and control GPIOs
#include "sdkconfig.h"          // Includes the project’s configuration file

// LED PWM configuration
#define LED_GPIO_PIN              5
#define LED_PWM_CHANNEL           LEDC_CHANNEL_0        //assign an available channel
#define LED_PWM_TIMER             LEDC_TIMER_0
#define LED_PWM_MODE              LEDC_LOW_SPEED_MODE   // some ESP32 also support LEDC_HIGH_SPEED_MODE
#define LED_PWM_DUTY_RESOLUTION   LEDC_TIMER_10_BIT     // 10-bit resolution (0-1023)
#define LED_PWM_FREQUENCY_HZ      1000  // 1 kHz avoids flicker, efficient for LED dimming

void app_main(void)
{
    // Configure PWM timer
    ledc_timer_config_t led_pwm_timer = {
        .speed_mode       = LED_PWM_MODE,
        .duty_resolution  = LED_PWM_DUTY_RESOLUTION,
        .timer_num        = LED_PWM_TIMER,
        .freq_hz          = LED_PWM_FREQUENCY_HZ
    };
    ledc_timer_config(&led_pwm_timer);

    // Configure PWM channel for LED
    ledc_channel_config_t led_pwm_channel = {
        .gpio_num   = LED_GPIO_PIN,
        .speed_mode = LED_PWM_MODE,
        .channel    = LED_PWM_CHANNEL,
        .timer_sel  = LED_PWM_TIMER,
        .duty       = 0 //set the initial duty cycle (example: 0 = off and 1023 = full brightness)
    };
    ledc_channel_config(&led_pwm_channel);

    // Fade loop
    while (true) {
        // Fade in
        for (int duty = 0; duty <= 1023; duty += 10) {
            ledc_set_duty(LED_PWM_MODE, LED_PWM_CHANNEL, duty);
            ledc_update_duty(LED_PWM_MODE, LED_PWM_CHANNEL);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        // Fade out
        for (int duty = 1023; duty >= 0; duty -= 10) {
            ledc_set_duty(LED_PWM_MODE, LED_PWM_CHANNEL, duty);
            ledc_update_duty(LED_PWM_MODE, LED_PWM_CHANNEL);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }
}

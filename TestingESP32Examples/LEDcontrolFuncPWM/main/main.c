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

// Helper: configure and fade a single LED
void fade_led(int gpio_pin, ledc_channel_t channel)
{
    // Configure PWM channel STRUCT for this LED
    ledc_channel_config_t led_channel = {
        .gpio_num   = gpio_pin,
        .speed_mode = LED_PWM_MODE,
        .channel    = channel,
        .timer_sel  = LED_PWM_TIMER,
        .duty       = 0
    };
    ledc_channel_config(&led_channel); 
    /*- led_channel and led_pwm_timer are struct variables (ledc_channel_config_t and ledc_timer_config_t).
    - The functions expect a pointer to these structs, not a copy.
    - & is the address-of operator in C/C++. It gives the memory address of the variable.
    - So &led_channel means “pass the address of this struct so the function can read its fields.
    */
   
    // Fade loop
    while (true) {
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

    // Run each LED fade in its own FreeRTOS task
    xTaskCreate((TaskFunction_t)fade_led, "fade_led1", 2048, (void*)LED1_GPIO_PIN, 5, NULL);
    xTaskCreate((TaskFunction_t)fade_led, "fade_led2", 2048, (void*)LED2_GPIO_PIN, 5, NULL);
    xTaskCreate((TaskFunction_t)fade_led, "fade_led3", 2048, (void*)LED3_GPIO_PIN, 5, NULL);
}

/*
xTaskCreate(
    (TaskFunction_t)fade_led,   // The function that will run as the task. 
                                // Here we cast fade_led to TaskFunction_t because FreeRTOS expects a specific signature.
                                // Each task must be a function with the signature: void task_name(void *pvParameters).
                                // This function will contain the infinite fade loop for one LED.

    "fade_led1",                // A human-readable name for the task (useful for debugging).
                                // It does not affect execution, but helps identify tasks in logs or debuggers.

    2048,                       // The stack size for this task, in words (not bytes).
                                // On ESP32, 1 word = 4 bytes, so 2048 words = 8192 bytes (8 KB).
                                // This memory is reserved for the task’s local variables, function calls,
                                and printf usage. If too small, the task may crash.


    (void*)LED1_GPIO_PIN,       // Parameter passed into the task function.
                                // Here we pass the GPIO pin number (cast to void*) so fade_led knows which LED to control.
                                // FreeRTOS always passes a void* pointer, so we cast the integer pin number (5) into void*.
                                // Inside fade_led(), we cast it back to int to know which GPIO to use.

    5,                          // Task priority. Higher numbers mean higher priority.
                                // Priority 5 is moderate; it ensures the LED fading runs smoothly without starving system tasks.

    NULL                        // Optional handle to the created task.
                                // If you don’t need to later reference or control the task (e.g., suspend/kill it), you can pass NULL.
);
*/

/*✅ Takeaway mental model:
- void* = “generic box” → you put something in (cast), then take it out (cast back).
- 2048 = stack size in words → multiply by 4 to get bytes on ESP32.
*/
#include <stdio.h> //The standard C library will be used for the printf function
#include "freertos/FreeRTOS.h" //Provides the core FreeRTOS types and functions
#include "freertos/task.h" //Allows to use of the 'vTaskDelay' non-blocking delay function 
#include "driver/gpio.h" //Includes the functions required to configure and control GPIOs
#include "sdkconfig.h" //includes the project’s configuration file

// Define the GPIO pin 2, 4 and 5 for LEDs (GPIO 2 is common for onboard LEDs)
#define LED_PIN_2 2
#define LED_PIN_4 4
#define LED_PIN_5 5

void app_main(void)
{
    // Configure GPIO
    gpio_config_t io_conf = {
        // Select GPIO pin to configure
        .pin_bit_mask = (1ULL << LED_PIN_2) | (1ULL << LED_PIN_4) | (1ULL << LED_PIN_5),
        .mode = GPIO_MODE_OUTPUT,              // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,     // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE         // Disable interrupts
    };
    gpio_config(&io_conf);

    // Blink loop
    while (1) {
        printf("LED Pin 2 ON\n");
        gpio_set_level(LED_PIN_2, 1);
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay 2 second

        printf("LED Pin 2 OFF\n");
        gpio_set_level(LED_PIN_2, 0);
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay 2 second

        printf("LED Pin 4 ON\n");
        gpio_set_level(LED_PIN_4, 1);
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay 2 second

        printf("LED Pin 4 OFF\n");
        gpio_set_level(LED_PIN_4, 0);
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay 2 second

        printf("LED Pin 5 ON\n");
        gpio_set_level(LED_PIN_5, 1);
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay 2 second

        printf("LED Pin 5 OFF\n");
        gpio_set_level(LED_PIN_5, 0);
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay 2 second
    }
}
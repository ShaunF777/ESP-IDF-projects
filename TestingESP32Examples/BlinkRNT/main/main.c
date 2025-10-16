#include <stdio.h> //The standard C library will be used for the printf function
#include "freertos/FreeRTOS.h" //Provides the core FreeRTOS types and functions
#include "freertos/task.h" //Allows to use of the 'vTaskDelay' non-blocking delay function 
#include "driver/gpio.h" //Includes the functions required to configure and control GPIOs
#include "sdkconfig.h" //includes the project’s configuration file

// Define the GPIO pin for the LED (GPIO 2 is common for onboard LEDs)
#define BLINK_GPIO 2

void app_main(void)
{
    // Configure the GPIO pin
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    // Blink loop
    while (1) {
        // Turn LED ON
        printf("LED ON\n");
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay 1 second

        // Turn LED OFF
        printf("LED OFF\n");
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay 1 second
    }
}






/*  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  https://RandomNerdTutorials.com/esp-idf-esp32-blink-led/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

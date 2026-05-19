#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BLUE_LED_PIN    
#define GREEN_LED_PIN   
#define RED_LED_PIN    
#define YELLOW_LED_PIN  

#define UART_PORT   UART_NUM_0
#define BUF_SIZE    1024

static const char *TAG = "UART";

// Configura os LEDS
esp_err_t led_config(int gpio_pin){
    gpio_config_t led_config = {
        .pin_bit_mask  = 1ULL << gpio_pin,
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };

    return gpio_config(&led_config);
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_NONE);

    // Configura os LEDS
    //led_config(BLUE_LED_PIN);
    //led_config(GREEN_LED_PIN);
    //led_config(RED_LED_PIN);
    //led_config(YELLOW_LED_PIN);

    // Configura UART0
    uart_config_t uart_config = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE, BUF_SIZE, 0, NULL, 0));

    uint8_t data[128];

    while(1){
        int length = uart_read_bytes(UART_PORT, data, sizeof(data), pdMS_TO_TICKS(100));

        if (length > 0){
            uart_write_bytes(UART_PORT, (const char*)data, length);
            uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"

#define BLUE_LED_PIN    4
#define GREEN_LED_PIN   5
#define RED_LED_PIN     6
#define YELLOW_LED_PIN  7

#define UART_PORT       UART_NUM_0
#define BUF_SIZE        1024
#define MAX_SEQUENCE    20

// Tempo maximo (em ms) que o jogador tem para repetir a sequencia inteira.
// Se estourar, o jogo encerra a rodada por tempo esgotado.
#define RESPONSE_TIMEOUT_MS  10000

static const int led_pins[4]       = {BLUE_LED_PIN, GREEN_LED_PIN, RED_LED_PIN, YELLOW_LED_PIN};
static const char *led_names[4]    = {"Azul", "Verde", "Vermelho", "Amarelo"};

static uint8_t          sequence[MAX_SEQUENCE];
static uint8_t          user_input[MAX_SEQUENCE];
static volatile int     current_level = 0;
static volatile int     user_pos      = 0;
static volatile bool    input_allowed = false;
static volatile bool    start_input   = false;
static volatile bool    input_error   = false;

// input_done_sem: dado por uart_rx_task quando o usuario termina de digitar a sequencia
// uart_mutex:     garante que escritas multi-byte na UART nao se intercalem entre tasks
static SemaphoreHandle_t input_done_sem;
static SemaphoreHandle_t uart_mutex;

static esp_err_t led_config(int gpio_pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio_pin,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static void blink_led(int idx, int on_ms, int off_ms) {
    gpio_set_level(led_pins[idx], 1);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    gpio_set_level(led_pins[idx], 0);
    vTaskDelay(pdMS_TO_TICKS(off_ms));
}

static void uart_send(const char *msg) {
    xSemaphoreTake(uart_mutex, portMAX_DELAY);
    uart_write_bytes(UART_PORT, msg, strlen(msg));
    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(200));
    xSemaphoreGive(uart_mutex);
}

static void display_sequence(int level) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (int i = 0; i < level; i++) {
        blink_led(sequence[i], 500, 250);
        if (i < level - 1) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
}

static void game_over_animation(void) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) gpio_set_level(led_pins[j], 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        for (int j = 0; j < 4; j++) gpio_set_level(led_pins[j], 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void win_animation(void) {
    for (int i = 0; i < 4; i++) blink_led(i, 200, 100);
    for (int i = 3; i >= 0; i--) blink_led(i, 200, 100);
}

static void game_task(void *arg) {
    char buf[80];

    vTaskDelay(pdMS_TO_TICKS(500));
    uart_send("\r\n=== GENIUS ESP32 ===\r\n");
    uart_send("1=Azul  2=Verde  3=Vermelho  4=Amarelo\r\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    while (1) {
        sequence[current_level] = (uint8_t)(esp_random() % 4);
        current_level++;

        snprintf(buf, sizeof(buf), "\r\n--- Nivel %d ---\r\nObserve a sequencia...\r\n", current_level);
        uart_send(buf);

        // Bloqueia input durante a exibicao da sequencia
        input_allowed = false;
        input_error   = false;
        user_pos      = 0;

        // Limpa qualquer sinal pendente de uma rodada anterior (ex.: corrida
        // entre o timeout e um xSemaphoreGive tardio do uart_rx_task)
        xSemaphoreTake(input_done_sem, 0);

        display_sequence(current_level);

        snprintf(buf, sizeof(buf), "Sua vez! Repita a sequencia (em ate %d s):\r\n", RESPONSE_TIMEOUT_MS / 1000);
        uart_send(buf);

        // Pede ao uart_rx_task (unico dono da leitura) para descartar as teclas
        // digitadas durante a exibicao e liberar o input. Chamar uart_flush_input
        // aqui disputaria o mutex interno de RX com o uart_read_bytes do rx_task
        // e travaria o jogo.
        start_input = true;

        // Aguarda o jogador terminar de digitar OU o tempo limite estourar
        bool responded = (xSemaphoreTake(input_done_sem, pdMS_TO_TICKS(RESPONSE_TIMEOUT_MS)) == pdTRUE);
        input_allowed = false;

        if (!responded) {
            uart_send("\r\n*** TEMPO ESGOTADO! Game Over! ***\r\n");
            game_over_animation();
            uart_send("Reiniciando...\r\n");
            vTaskDelay(pdMS_TO_TICKS(1500));
            current_level = 0;
        } else if (input_error) {
            uart_send("\r\n*** ERRADO! Game Over! ***\r\n");
            game_over_animation();
            uart_send("Reiniciando...\r\n");
            vTaskDelay(pdMS_TO_TICKS(1500));
            current_level = 0;
        } else if (current_level >= MAX_SEQUENCE) {
            uart_send("\r\n*** PARABENS! Voce completou o jogo! ***\r\n");
            win_animation();
            vTaskDelay(pdMS_TO_TICKS(1500));
            current_level = 0;
        } else {
            uart_send("Correto! Proximo nivel...\r\n");
        }
    }
}

static void uart_rx_task(void *arg) {
    uint8_t byte;
    char    buf[48];

    while (1) {
        if (start_input) {
            uart_flush_input(UART_PORT);
            start_input   = false;
            input_allowed = true;
        }

        int len = uart_read_bytes(UART_PORT, &byte, 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        // Descarta silenciosamente enquanto os LEDs estao piscando
        // ou apos o usuario ja ter inserido todos os digitos
        if (!input_allowed) continue;

        if (byte < '1' || byte > '4') {
            uart_send("Invalido! Use apenas 1, 2, 3 ou 4.\r\n");
            continue;
        }

        int idx = byte - '1';

        snprintf(buf, sizeof(buf), "[%d/%d] %s\r\n", user_pos + 1, current_level, led_names[idx]);
        uart_send(buf);

        // Pisca o LED escolhido como feedback visual
        blink_led(idx, 300, 100);

        user_input[user_pos] = (uint8_t)idx;

        // Erro acusado imediatamente: se o digito atual ja diverge da sequencia,
        // encerra a vez do usuario sem esperar o resto da sequencia.
        if ((uint8_t)idx != sequence[user_pos]) {
            input_error   = true;
            input_allowed = false;
            xSemaphoreGive(input_done_sem);
            continue;
        }

        user_pos++;

        // Sequencia repetida corretamente ate o fim: sinaliza sucesso
        if (user_pos >= current_level) {
            input_allowed = false;
            xSemaphoreGive(input_done_sem);
        }
    }
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_NONE);

    for (int i = 0; i < 4; i++) {
        led_config(led_pins[i]);
        gpio_set_level(led_pins[i], 0);
    }

    if (uart_is_driver_installed(UART_PORT)) {
        uart_driver_delete(UART_PORT);
    }

    uart_config_t uart_cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE, BUF_SIZE, 0, NULL, 0));

    input_done_sem = xSemaphoreCreateBinary();
    uart_mutex     = xSemaphoreCreateMutex();

    xTaskCreate(uart_rx_task, "uart_rx", 4096, NULL, 5, NULL);
    xTaskCreate(game_task,    "game",    4096, NULL, 4, NULL);
}

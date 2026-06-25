/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "onewire_bus.h"
#include "ds18b20.h"

static const char *TAG = "example";

#define EXAMPLE_ONEWIRE_BUS_GPIO    4
#define EXAMPLE_ONEWIRE_MAX_DS18B20 2

// Variável que indica que houve uma vibração
static volatile bool keyes_detectado = false;

// Função de interrupção do sensor Keyes
static void IRAM_ATTR keyes_isr_handler(void *arg)
{
    keyes_detectado = true;
}

void app_main(void)
{
    // install new 1-wire bus
    onewire_bus_handle_t bus;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
        .flags = {
            .en_pull_up = true, // enable the internal pull-up resistor in case the external device didn't have one
        }
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10, // 1byte ROM command + 8byte ROM number + 1byte device command
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));
    ESP_LOGI(TAG, "1-Wire bus installed on GPIO%d", EXAMPLE_ONEWIRE_BUS_GPIO);

    int ds18b20_device_num = 0;
    ds18b20_device_handle_t ds18b20s[EXAMPLE_ONEWIRE_MAX_DS18B20];
    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t search_result = ESP_OK;

    // create 1-wire device iterator, which is used for device search
    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    ESP_LOGI(TAG, "Device iterator created, start searching...");
    do {
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (search_result == ESP_OK) { // found a new device, let's check if we can upgrade it to a DS18B20
            ds18b20_config_t ds_cfg = {};
            onewire_device_address_t address;
            // wrap the generic 1-wire device into a DS18B20 sensor device
            if (ds18b20_new_device_from_enumeration(&next_onewire_device, &ds_cfg, &ds18b20s[ds18b20_device_num]) == ESP_OK) {
                ds18b20_get_device_address(ds18b20s[ds18b20_device_num], &address);
                ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num, address);
                ds18b20_device_num++;
                if (ds18b20_device_num >= EXAMPLE_ONEWIRE_MAX_DS18B20) {
                    ESP_LOGI(TAG, "Max DS18B20 number reached, stop searching...");
                    break;
                }
            } else {
                ESP_LOGI(TAG, "Found an unknown device, address: %016llX", next_onewire_device.address);
            }
        }
    } while (search_result != ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    ESP_LOGI(TAG, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);

    // set resolution for all DS18B20s
    for (int i = 0; i < ds18b20_device_num; i++) {
        // set resolution
        ESP_ERROR_CHECK(ds18b20_set_resolution(ds18b20s[i], DS18B20_RESOLUTION_12B));
    }

    //Configuração do sensor keyes
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_5),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    gpio_config(&io_conf);

    // Instala o serviço de interrupções
    gpio_install_isr_service(0);

    // Associa a interrupção ao GPIO 5
    gpio_isr_handler_add(
       GPIO_NUM_5,
       keyes_isr_handler,
       NULL
    );

    // get temperature from sensors one by one
    float temperature;
    while (1) {

        if(keyes_detectado){
            keyes_detectado = false;
            ESP_LOGI("KEYES", "Vibracao detectada");
        }

        // trigger temperature conversion for all DS18B20s on the bus

        if(ds18b20_device_num > 0){
            ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion_for_all(bus));
        // read temperature from each DS18B20
            for (int i = 0; i < ds18b20_device_num; i ++) {
                ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[i], &temperature));
                ESP_LOGI(TAG, "temperature read from DS18B20[%d]: %.2fC", i, temperature);
            }
        }
    }
}

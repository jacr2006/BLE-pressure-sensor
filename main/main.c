#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_fat.h"
//#include "cmd_system.h"
#include "cmd_i2ctools.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include <math.h>

#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

#include "../components/bmp280/driver/bmp280.h"

static gpio_num_t i2c_gpio_sda = 21;
static gpio_num_t i2c_gpio_scl = 22;
static uint32_t i2c_frequency = 100000;
static i2c_port_t i2c_port = I2C_NUM_0;

static const char *TAG = "bmp280_test";

static esp_ble_adv_params_t ble_adv_params = {

	.adv_int_min = 0x600,
	.adv_int_max = 0x680,
	.adv_type = ADV_TYPE_NONCONN_IND,
	.own_addr_type  = BLE_ADDR_TYPE_PUBLIC,
	.channel_map = ADV_CHNL_ALL,
	.adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

//
uint8_t adv_raw_data[6];

// GAP callback
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {

		case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:

			printf("ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT\n");
			esp_ble_gap_start_advertising(&ble_adv_params);
			break;

		case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:

			printf("ESP_GAP_BLE_ADV_START_COMPLETE_EVT\n");
			if(param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
				printf("Advertising started\n\n");
			}
			else printf("Unable to start advertising process, error code %d\n\n", param->scan_start_cmpl.status);
			break;

		default:

			printf("Event %d unhandled\n\n", event);
			break;
	}
}

void delay_ms(uint32_t period_ms) {
	vTaskDelay(1000 / portTICK_PERIOD_MS);
}

static esp_err_t i2c_master_driver_initialize(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = i2c_gpio_sda,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = i2c_gpio_scl,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = i2c_frequency,
        // .clk_flags = 0,          /*!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here. */
    };
    return i2c_param_config(i2c_port, &conf);
}

int8_t i2c_reg_write(uint8_t i2c_addr, uint8_t reg_addr, uint8_t *reg_data, uint16_t length) {

	i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
	i2c_master_driver_initialize();
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, i2c_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
	i2c_master_write_byte(cmd, reg_addr, ACK_CHECK_EN);
	for (int i = 0; i < length; i++) {
		i2c_master_write_byte(cmd, reg_data[i], ACK_CHECK_EN);
	}
	i2c_master_stop(cmd);
	esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if (ret == ESP_OK) {
		ESP_LOGI(TAG, "Write OK");
	} else if (ret == ESP_ERR_TIMEOUT) {
		ESP_LOGW(TAG, "Bus is busy");
	} else {
		ESP_LOGW(TAG, "Write Failed");
	}
	i2c_driver_delete(i2c_port);
	return 0;

}

int8_t i2c_reg_read(uint8_t i2c_addr, uint8_t reg_addr, uint8_t *reg_data, uint16_t length) {

	i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
	i2c_master_driver_initialize();
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	if (reg_addr != -1) {
		i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
		i2c_master_write_byte(cmd, reg_addr, ACK_CHECK_EN);
		i2c_master_start(cmd);
	}
	i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
	if (length > 1) {
		i2c_master_read(cmd, reg_data, length - 1, ACK_VAL);
	}
	i2c_master_read_byte(cmd, reg_data + length - 1, NACK_VAL);
	i2c_master_stop(cmd);
	esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if (ret == ESP_OK) {
		ESP_LOGW(TAG, "Read ok");
	} else if (ret == ESP_ERR_TIMEOUT) {
		ESP_LOGW(TAG, "Bus is busy");
	} else {
		ESP_LOGW(TAG, "Read failed");
	}
	i2c_driver_delete(i2c_port);
	return 0;
}


void print_rslt(const char api_name[], int8_t rslt)
{
    if (rslt != BMP280_OK)
    {
        printf("%s\t", api_name);
        if (rslt == BMP280_E_NULL_PTR)
        {
            printf("Error [%d] : Null pointer error\r\n", rslt);
        }
        else if (rslt == BMP280_E_COMM_FAIL)
        {
            printf("Error [%d] : Bus communication failed\r\n", rslt);
        }
        else if (rslt == BMP280_E_IMPLAUS_TEMP)
        {
            printf("Error [%d] : Invalid Temperature\r\n", rslt);
        }
        else if (rslt == BMP280_E_DEV_NOT_FOUND)
        {
            printf("Error [%d] : Device not found\r\n", rslt);
        }
        else
        {
            /* For more error codes refer "*_defs.h" */
            printf("Error [%d] : Unknown error code\r\n", rslt);
        }
    }
}

static void i2c_task(void *arg)
{
    int8_t rslt;
    struct bmp280_dev bmp;
    struct bmp280_config conf;
    struct bmp280_uncomp_data ucomp_data;
    uint32_t pres32, pres64;
    double pres;

    /* Map the delay function pointer with the function responsible for implementing the delay */
    bmp.delay_ms = delay_ms;

    /* Assign device I2C address based on the status of SDO pin (GND for PRIMARY(0x76) & VDD for SECONDARY(0x77)) */
    bmp.dev_id = BMP280_I2C_ADDR_PRIM;

    /* Select the interface mode as I2C */
    bmp.intf = BMP280_I2C_INTF;

    /* Map the I2C read & write function pointer with the functions responsible for I2C bus transfer */
    bmp.read = i2c_reg_read;
    bmp.write = i2c_reg_write;

    /* To enable SPI interface: comment the above 4 lines and uncomment the below 4 lines */

    /*
     * bmp.dev_id = 0;
     * bmp.read = spi_reg_read;
     * bmp.write = spi_reg_write;
     * bmp.intf = BMP280_SPI_INTF;
     */
    rslt = bmp280_init(&bmp);
    print_rslt(" bmp280_init status", rslt);

    /* Always read the current settings before writing, especially when
     * all the configuration is not modified
     */
    rslt = bmp280_get_config(&conf, &bmp);
    print_rslt(" bmp280_get_config status", rslt);

    /* configuring the temperature oversampling, filter coefficient and output data rate */
    /* Overwrite the desired settings */
    conf.filter = BMP280_FILTER_COEFF_2;

    /* Pressure oversampling set at 4x */
    conf.os_pres = BMP280_OS_4X;

    /* Setting the output data rate as 1HZ(1000ms) */
    conf.odr = BMP280_ODR_1000_MS;
    rslt = bmp280_set_config(&conf, &bmp);
    print_rslt(" bmp280_set_config status", rslt);

    /* Always set the power mode after setting the configuration */
    rslt = bmp280_set_power_mode(BMP280_NORMAL_MODE, &bmp);
    print_rslt(" bmp280_set_power_mode status", rslt);
    while (1)
    {
        /* Reading the raw data from sensor */
        rslt = bmp280_get_uncomp_data(&ucomp_data, &bmp);

        /* Getting the compensated pressure using 32 bit precision */
        rslt = bmp280_get_comp_pres_32bit(&pres32, ucomp_data.uncomp_press, &bmp);

        /* Getting the compensated pressure using 64 bit precision */
        rslt = bmp280_get_comp_pres_64bit(&pres64, ucomp_data.uncomp_press, &bmp);

        /* Getting the compensated pressure as floating point value */
        rslt = bmp280_get_comp_pres_double(&pres, ucomp_data.uncomp_press, &bmp);

//        printf( "UP: %lu, P32: %lu, P64: %lu, P64N: %lu, P: %f\r\n",
//        		(long unsigned int)ucomp_data.uncomp_press,
//               (long unsigned int)pres32,
//			   (long unsigned int)pres64,
//			   (long unsigned int)(pres64 / 256),
//               pres );

        printf( "P: %lu mb\r\n", (unsigned long)pres32 );
        printf( "P: %08lX mb\r\n", (unsigned long)pres32 );

        // configure the adv data
        adv_raw_data[0] = 0x05;
		adv_raw_data[1] = 0xFF;
		adv_raw_data[2] = (uint8_t)(pres32>>24)&0xFF;
		adv_raw_data[3] = (uint8_t)(pres32>>16)&0xFF;
		adv_raw_data[4] = (uint8_t)(pres32>>8)&0xFF;
		adv_raw_data[5] = (uint8_t)(pres32)&0xFF;

		ESP_ERROR_CHECK(esp_ble_gap_config_adv_data_raw(adv_raw_data, 6));
		printf("- ADV data configured\n\n");

        bmp.delay_ms(1000); /* Sleep time between measurements = BMP280_ODR_1000_MS */
    }
    vTaskDelete(NULL);
}

void app_main() {

	printf("BT broadcast\n\n");

	// set components to log only errors
	esp_log_level_set("*", ESP_LOG_ERROR);

	// initialize nvs
	ESP_ERROR_CHECK(nvs_flash_init());
	printf("- NVS init ok\n");

	// release memory reserved for classic BT (not used)
	ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
	printf("- Memory for classic BT released\n");

	// initialize the BT controller with the default config
	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
	printf("- BT controller init ok\n");

	// enable the BT controller in BLE mode
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
	printf("- BT controller enabled in BLE mode\n");

	// initialize Bluedroid library
	esp_bluedroid_init();
    esp_bluedroid_enable();
	printf("- Bluedroid initialized and enabled\n");

	// register GAP callback function
	ESP_ERROR_CHECK(esp_ble_gap_register_callback(esp_gap_cb));
	printf("- GAP callback registered\n\n");

	ESP_ERROR_CHECK(esp_ble_gap_set_device_name((const char *)"ESP32_BLE"));

	// configure the adv data
	ESP_ERROR_CHECK(esp_ble_gap_config_adv_data_raw(adv_raw_data, 6));
	printf("- ADV data configured\n\n");

	//
	xTaskCreate(i2c_task, "i2c_task", 1024 * 64, (void *)0, 10, NULL);
}

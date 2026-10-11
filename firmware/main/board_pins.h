#pragma once
#include "driver/gpio.h"

#define PIN_I2C_SDA         GPIO_NUM_1
#define PIN_I2C_SCL         GPIO_NUM_2
#define PIN_RTC_SQW         GPIO_NUM_4

#define PMS_UART_NUM        UART_NUM_1
#define PIN_PMS_TX          GPIO_NUM_17
#define PIN_PMS_RX          GPIO_NUM_18
#define PIN_PMS_SET         GPIO_NUM_15
#define PIN_PMS_RESET       GPIO_NUM_16

#define PIN_SD_CLK          GPIO_NUM_12
#define PIN_SD_CMD          GPIO_NUM_11
#define PIN_SD_D0           GPIO_NUM_13
#define PIN_SD_D1           GPIO_NUM_14
#define PIN_SD_D2           GPIO_NUM_9
#define PIN_SD_D3           GPIO_NUM_10
#define PIN_SD_CD           GPIO_NUM_21

#define PIN_LED_R           GPIO_NUM_5
#define PIN_LED_G           GPIO_NUM_6
#define PIN_LED_B           GPIO_NUM_7

#define PIN_BUTTON          GPIO_NUM_0
#define PIN_VSENSE          GPIO_NUM_8
#define PIN_PWR_GOOD        GPIO_NUM_38

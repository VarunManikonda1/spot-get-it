#ifndef SYSTEM_HAL_H
#define SYSTEM_HAL_H

#include "stm32f4xx_hal.h"

/* === Global handles (system_hal.c에서 정의) === */
extern UART_HandleTypeDef huart3;   /* RR servo bus  (USART3, PB10, HDSEL 1Mbps) */
extern UART_HandleTypeDef huart4;   /* FR servo bus  (UART4,  PA0,  HDSEL 1Mbps) */
extern UART_HandleTypeDef huart5;   /* RL servo bus  (UART5,  PC12, HDSEL 1Mbps) */
extern UART_HandleTypeDef huart6;   /* FL servo bus  (USART6, PC6,  HDSEL 1Mbps) */
extern UART_HandleTypeDef huart2;   /* ST-Link VCP debug + ESC input (PA2/PA3, 115200) */
extern I2C_HandleTypeDef  hi2c1;    /* BNO055 (PB8/PB9, Fast-Mode 400kHz). PB6/PB7 은 USART1 으로 양보됨. */

/* HAL_Init() 후 한 번만 호출. 시스템 클럭 + 모든 GPIO/UART/I2C 초기화. */
void system_hal_init_all(void);

void Error_Handler(void);

#endif /* SYSTEM_HAL_H */

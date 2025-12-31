#ifndef LIQUIDCRYSTAL_I2C_H
#define LIQUIDCRYSTAL_I2C_H

#include <stdint.h>
#include "stm32f1xx_hal.h" // 자신의 MCU에 맞는 HAL 드라이버 헤더

void LCD_Init(I2C_HandleTypeDef *hi2c, uint8_t address, uint8_t cols, uint8_t rows);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t col, uint8_t row);
void LCD_WriteString(char *str);
void LCD_SendCommand(uint8_t cmd);
void LCD_SendData(uint8_t data);

#endif //LIQUIDCRYSTAL_I2C_H

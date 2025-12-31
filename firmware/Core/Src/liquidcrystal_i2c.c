#include "liquidcrystal_i2c.h"

// LCD 내부 변수
static I2C_HandleTypeDef *g_hi2c;
static uint8_t g_lcd_address;
static uint8_t g_num_rows;
static uint8_t g_backlight = 0x08; // 백라이트 ON

// LCD 컨트롤 비트 상수
#define EN_BIT 0x04  // Enable bit
#define RS_BIT 0x01  // Register select bit

// 내부 함수 선언
static void LCD_Write_I2C(uint8_t data, uint8_t flags);
static void LCD_Write_Nibble(uint8_t nibble, uint8_t flags);

// I2C를 통해 1바이트 전송 (4비트 데이터 + 컨트롤 비트)
static void LCD_Write_I2C(uint8_t data, uint8_t flags) {
    uint8_t buffer[1];
    buffer[0] = data | flags | g_backlight;
    HAL_I2C_Master_Transmit(g_hi2c, g_lcd_address << 1, buffer, 1, HAL_MAX_DELAY);
}

// 4비트(nibble) 데이터 전송
static void LCD_Write_Nibble(uint8_t nibble, uint8_t flags) {
    LCD_Write_I2C(nibble, flags | EN_BIT);
    HAL_Delay(1);
    LCD_Write_I2C(nibble, flags); // Enable 펄스 완료
    HAL_Delay(1);
}

// 공개 함수 구현
void LCD_SendCommand(uint8_t cmd) {
    uint8_t upper_nibble = cmd & 0xF0;
    uint8_t lower_nibble = (cmd << 4) & 0xF0;

    LCD_Write_Nibble(upper_nibble, 0); // RS=0 (Command)
    LCD_Write_Nibble(lower_nibble, 0); // RS=0 (Command)
}

void LCD_SendData(uint8_t data) {
    uint8_t upper_nibble = data & 0xF0;
    uint8_t lower_nibble = (data << 4) & 0xF0;

    LCD_Write_Nibble(upper_nibble, RS_BIT); // RS=1 (Data)
    LCD_Write_Nibble(lower_nibble, RS_BIT); // RS=1 (Data)
}

void LCD_Init(I2C_HandleTypeDef *hi2c, uint8_t address, uint8_t cols, uint8_t rows) {
    g_hi2c = hi2c;
    g_lcd_address = address;
    g_num_rows = rows;

    HAL_Delay(50); // 전원 인가 후 대기

    // 4비트 모드 초기화 시퀀스
    LCD_Write_Nibble(0x30, 0);
    HAL_Delay(5);
    LCD_Write_Nibble(0x30, 0);
    HAL_Delay(1);
    LCD_Write_Nibble(0x30, 0);
    HAL_Delay(1);
    LCD_Write_Nibble(0x20, 0); // 4비트 모드로 설정
    HAL_Delay(1);

    // 4비트 모드 설정 후 초기화
    LCD_SendCommand(0x28); // Function set: 4-bit, 2-line, 5x8 dot
    HAL_Delay(1);
    LCD_SendCommand(0x0C); // Display on/off control: display on, cursor off, blink off
    HAL_Delay(1);
    LCD_SendCommand(0x06); // Entry mode set: increment cursor, no shift
    HAL_Delay(1);
    LCD_SendCommand(0x01); // Clear display
    HAL_Delay(2);
}

void LCD_Clear(void) {
    LCD_SendCommand(0x01);
    HAL_Delay(2);
}

void LCD_SetCursor(uint8_t col, uint8_t row) {
    uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    if (row >= g_num_rows) {
        row = g_num_rows - 1;
    }
    LCD_SendCommand(0x80 | (col + row_offsets[row]));
}

void LCD_WriteString(char *str) {
    while (*str) {
        LCD_SendData((uint8_t)(*str++));
    }
}

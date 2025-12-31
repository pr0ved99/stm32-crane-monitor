/* NUCLEO-F103RB — Joystick(ADC) + Buttons LCD & 시리얼 동시 출력
 * 핀맵:
 * - LED   : PA5 (LD2)
 * - I2C1  : PB8=SCL, PB9=SDA
 * - USART2: PA2=TX, PA3=RX
 * - ADC   : PA0(LX), PA1(LY), PA4(RX), PB0(RY)
 * - BTN   : PC10(L_SW), PC11(R_SW), PC12(UP), PD2(DOWN)
 */

#include "stm32f1xx_hal.h"
#include <string.h>
#include <stdio.h>

/* ===== 프로토타입 ===== */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
void Error_Handler(void);

static uint16_t ADC_ReadChannel(uint32_t ch);
static inline uint8_t scale0_10(uint16_t raw);
static inline uint8_t btn_read(GPIO_TypeDef* port, uint16_t pin);

static void LCD_Init(void);
static void LCD_Clear(void);
static void LCD_SetCursor(uint8_t col, uint8_t row);
static void LCD_Print(const char* s);

/* ===== 핸들 ===== */
ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart2;
I2C_HandleTypeDef hi2c1;

/* ===== LCD (PCF8574 4-bit) ===== */
#define LCD_BL 0x08
#define LCD_EN 0x04
#define LCD_RS 0x01
static uint16_t g_lcd_addr = 0;
// ⭐️ FIX: 누락된 lcd_backlight 변수 선언 추가
static uint8_t lcd_backlight = LCD_BL;

/* ===== printf 리디렉션 ===== */
#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif
PUTCHAR_PROTOTYPE
{
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}

/* ====== 헬퍼 ====== */
static inline uint8_t btn_read(GPIO_TypeDef* port, uint16_t pin) {
  return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1u : 0u;
}
static inline uint8_t scale0_10(uint16_t raw) {
  uint32_t v = raw + 204; if (v > 4095) v = 4095;
  return (uint8_t)(v / 410);
}

/* ===================== main ===================== */
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();

  HAL_ADCEx_Calibration_Start(&hadc1);
  LCD_Init();

  printf("Controller Monitor Ready\r\n");
  LCD_Print("Ready");
  HAL_Delay(500); // Ready 메시지를 볼 수 있도록 잠시 대기

  uint32_t t_led = HAL_GetTick();
  uint32_t t_ui  = HAL_GetTick();

  uint8_t current_lcd_state = 0;
  uint8_t previous_lcd_state = 99;

  char line1[17] = {0};
  char line2[17] = {0};

  while (1) {
      uint32_t now = HAL_GetTick();

      if (now - t_led >= 200) {
        t_led = now;
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
      }

      if (now - t_ui >= 100) {
        t_ui = now;

        // 값 읽기는 기존과 동일
        int8_t Lx = (int8_t)scale0_10(ADC_ReadChannel(ADC_CHANNEL_0)) - 5;
        int8_t Ly = 5 - (int8_t)scale0_10(ADC_ReadChannel(ADC_CHANNEL_1));
        int8_t Rx = (int8_t)scale0_10(ADC_ReadChannel(ADC_CHANNEL_4)) - 5;
        int8_t Ry = 5 - (int8_t)scale0_10(ADC_ReadChannel(ADC_CHANNEL_8));
        uint8_t Ls = btn_read(GPIOC, GPIO_PIN_10);
        uint8_t Rs = btn_read(GPIOC, GPIO_PIN_11);
        uint8_t up_btn = btn_read(GPIOD, GPIO_PIN_2);
        uint8_t down_btn = btn_read(GPIOC, GPIO_PIN_12);

        // ⭐️ FIX: Node.js 서버가 파싱하기 쉬운 형태로 데이터 전송
        printf("LX:%d,LY:%d,LS:%u,RX:%d,RY:%d,RS:%u,UP:%u,DN:%u\n",
               Lx, Ly, Ls, Rx, Ry, Rs, up_btn, down_btn);
      if (up_btn) {
          current_lcd_state = 1;
      } else if (down_btn) {
          current_lcd_state = 2;
      } else {
          current_lcd_state = 0;
      }

      if (current_lcd_state != previous_lcd_state) {
          LCD_Clear();
          switch (current_lcd_state) {
              case 1:
                  LCD_SetCursor(0, 0);
                  LCD_Print("Going Down!");
                  break;
              case 2:
                  LCD_SetCursor(0, 0);
                  LCD_Print("Going Up!");
                  break;
          }
      }

      if (current_lcd_state == 0) {
          snprintf(line1, sizeof(line1), "LX:%+2d LY:%+2d L:%u", Lx, Ly, Ls);
          snprintf(line2, sizeof(line2), "RX:%+2d RY:%+2d R:%u", Rx, Ry, Rs);
          LCD_SetCursor(0,0); LCD_Print(line1);
          LCD_SetCursor(0,1); LCD_Print(line2);
      }

      previous_lcd_state = current_lcd_state;
    }
  }
}

/* ===================== ADC 단발 폴링 ===================== */
static uint16_t ADC_ReadChannel(uint32_t ch)
{
  ADC_ChannelConfTypeDef s = {0};
  s.Channel = ch;
  s.Rank = ADC_REGULAR_RANK_1;
  s.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

  HAL_ADC_ConfigChannel(&hadc1, &s);
  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
    HAL_ADC_Stop(&hadc1);
    return 0;
  }
  uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return v;
}

/* ===================== System Clock 64MHz ===================== */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef O = {0};
  RCC_ClkInitTypeDef C = {0};

  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

  O.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  O.HSIState            = RCC_HSI_ON;
  O.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  O.PLL.PLLState        = RCC_PLL_ON;
  O.PLL.PLLSource       = RCC_PLLSOURCE_HSI_DIV2;
  O.PLL.PLLMUL          = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&O) != HAL_OK) Error_Handler();

  C.ClockType      = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  C.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  C.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  C.APB1CLKDivider = RCC_HCLK_DIV2;
  C.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&C, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

/* ===================== GPIO ===================== */
static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};

  g.Pin = GPIO_PIN_5; g.Mode = GPIO_MODE_OUTPUT_PP; g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &g);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4;
  HAL_GPIO_Init(GPIOA, &g);
  g.Pin = GPIO_PIN_0;
  HAL_GPIO_Init(GPIOB, &g);

  g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;
  g.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOC, &g);
  g.Pin = GPIO_PIN_2;
  HAL_GPIO_Init(GPIOD, &g);
}

/* ===================== I2C1 (PB8/PB9) ===================== */
static void MX_I2C1_Init(void)
{
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_AFIO_REMAP_I2C1_ENABLE();

  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  g.Mode = GPIO_MODE_AF_OD;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &g);

  __HAL_RCC_I2C1_CLK_ENABLE();

  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

/* ===================== USART2 (PA2/PA3) ===================== */
static void MX_USART2_UART_Init(void)
{
  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_2;
  g.Mode = GPIO_MODE_AF_PP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &g);
  g.Pin = GPIO_PIN_3;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);

  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

/* ===================== ADC1 (단발 폴링) ===================== */
static void MX_ADC1_Init(void)
{
  __HAL_RCC_ADC1_CLK_ENABLE();

  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();
}

/* ===================== LCD 구현 ===================== */
static void LCD_PulseEnable(uint8_t data)
{
  uint8_t b = data | LCD_EN | lcd_backlight;
  HAL_I2C_Master_Transmit(&hi2c1, g_lcd_addr, &b, 1, 10);
  b &= ~LCD_EN;
  HAL_I2C_Master_Transmit(&hi2c1, g_lcd_addr, &b, 1, 10);
}
static void LCD_WriteByte(uint8_t data, uint8_t rs)
{
  uint8_t d = (data & 0xF0) | (rs ? LCD_RS : 0);
  LCD_PulseEnable(d);
  d = ((data << 4) & 0xF0) | (rs ? LCD_RS : 0);
  LCD_PulseEnable(d);
}
static void LCD_Cmd(uint8_t cmd)   { LCD_WriteByte(cmd, 0); HAL_Delay(2); }
static void LCD_Data(uint8_t data) { LCD_WriteByte(data, 1); }
static void LCD_Clear(void) { LCD_Cmd(0x01); }
static void LCD_SetCursor(uint8_t col, uint8_t row)
{
  static const uint8_t offs[] = {0x00, 0x40};
  LCD_Cmd(0x80 | (offs[row] + col));
}
static void LCD_Print(const char* s) { while (*s) LCD_Data((uint8_t)(*s++)); }
static void LCD_Init(void)
{
  HAL_Delay(50);
  uint16_t a27 = (0x27 << 1), a3F = (0x3F << 1);
  if (HAL_I2C_IsDeviceReady(&hi2c1, a27, 2, 10) == HAL_OK) g_lcd_addr = a27;
  else if (HAL_I2C_IsDeviceReady(&hi2c1, a3F, 2, 10) == HAL_OK) g_lcd_addr = a3F;
  else Error_Handler();

  LCD_WriteByte(0x33, 0); HAL_Delay(5);
  LCD_WriteByte(0x32, 0); HAL_Delay(5);
  LCD_Cmd(0x28);
  LCD_Cmd(0x0C);
  LCD_Cmd(0x06);
  LCD_Clear();
}

/* ===================== 에러 핸들러 ===================== */
void Error_Handler(void)
{
  __disable_irq();
  while (1) {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    HAL_Delay(50);
  }
}

#include "system_hal.h"
#include "uart_jetson.h"

/* === printf retarget — 모든 printf 출력은 USART2 (ST-Link VCP) === */
int _write(int file, char *ptr, int len) {
    (void)file;
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

/* === System clock — HSI + PLL → 180MHz === */
static void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 8; osc.PLL.PLLN = 180;
    osc.PLL.PLLP = RCC_PLLP_DIV2; osc.PLL.PLLQ = 4; osc.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();
    if (HAL_PWREx_EnableOverDrive() != HAL_OK) Error_Handler();
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
}

static void MX_I2C1_Init(void) {
    __HAL_RCC_I2C1_CLK_ENABLE();
    /* I2C1 핀: PB8 (SCL) / PB9 (SDA) — Arduino D15/D14 위치.
     * PB6/PB7 은 USART1 alternate (PA9/PA10 의 USB OTG 충돌 회피용) 으로 양보. */
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gp.Mode = GPIO_MODE_AF_OD;
    gp.Pull = GPIO_PULLUP;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gp.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gp);

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_UART4_HDSEL_Init(void) {
    __HAL_RCC_UART4_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_0; gp.Mode = GPIO_MODE_AF_OD; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &gp);
    huart4.Instance = UART4;
    huart4.Init.BaudRate = 1000000; huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1; huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX_RX; huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_HalfDuplex_Init(&huart4) != HAL_OK) Error_Handler();
}

static void MX_USART6_HDSEL_Init(void) {
    __HAL_RCC_USART6_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_6; gp.Mode = GPIO_MODE_AF_OD; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOC, &gp);
    huart6.Instance = USART6;
    huart6.Init.BaudRate = 1000000; huart6.Init.WordLength = UART_WORDLENGTH_8B;
    huart6.Init.StopBits = UART_STOPBITS_1; huart6.Init.Parity = UART_PARITY_NONE;
    huart6.Init.Mode = UART_MODE_TX_RX; huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart6.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_HalfDuplex_Init(&huart6) != HAL_OK) Error_Handler();
}

static void MX_USART3_HDSEL_Init(void) {
    __HAL_RCC_USART3_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_10; gp.Mode = GPIO_MODE_AF_OD; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &gp);
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 1000000; huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1; huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX; huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_HalfDuplex_Init(&huart3) != HAL_OK) Error_Handler();
}

static void MX_UART5_HDSEL_Init(void) {
    __HAL_RCC_UART5_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_12; gp.Mode = GPIO_MODE_AF_OD; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF8_UART5;
    HAL_GPIO_Init(GPIOC, &gp);
    huart5.Instance = UART5;
    huart5.Init.BaudRate = 1000000; huart5.Init.WordLength = UART_WORDLENGTH_8B;
    huart5.Init.StopBits = UART_STOPBITS_1; huart5.Init.Parity = UART_PARITY_NONE;
    huart5.Init.Mode = UART_MODE_TX_RX; huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart5.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_HalfDuplex_Init(&huart5) != HAL_OK) Error_Handler();
}

static void MX_USART2_Init(void) {
    __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_2 | GPIO_PIN_3; gp.Mode = GPIO_MODE_AF_PP; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gp);
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200; huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1; huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX; huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

void system_hal_init_all(void) {
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_Init();
    MX_UART4_HDSEL_Init();
    MX_USART6_HDSEL_Init();
    MX_USART3_HDSEL_Init();
    MX_UART5_HDSEL_Init();
    MX_I2C1_Init();
    MX_USART1_Jetson_Init();
    HAL_Delay(200);
}

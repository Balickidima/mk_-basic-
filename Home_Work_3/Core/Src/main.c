/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "core_cm4.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

/* USER CODE BEGIN PV */
/* Переменные для обработки кнопки и PWM */
volatile uint8_t led_pwm_active = 0;    /* Флаг активного PWM режима: 0 - выкл, 1 - вкл */
uint8_t pwm_duty = 0;                   /* Текущий duty cycle PWM (0-100) */
uint8_t pwm_direction = 0;              /* Направление изменения PWM: 0 - рост, 1 - спад */

/* Переменные для SPI обмена */
uint8_t tx_byte = 0;           /* Байт для передачи (состояние PWM) */
volatile uint8_t rx_byte = 0;  /* Принятый байт на Master (от Slave) */
volatile uint8_t slave_rx = 0; /* Принятый байт на Slave */
uint8_t last_pwm_state = 0;    /* Для отслеживания изменения PWM */

/* Новые переменные для CRC */
volatile uint8_t spi_crc_error = 0;      /* Флаг ошибки CRC */
volatile uint8_t spi_total_errors = 0;   /* Счетчик общих ошибок */
uint8_t tx_crc = 0;                     /* CRC для передачи */
uint8_t rx_crc = 0;                     /* CRC для приема */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Задержка в микросекундах для PWM */
static void Delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t freq = HAL_RCC_GetHCLKFreq() / 100000;  /* Частота в МГц */
  while ((DWT->CYCCNT - start) < (us * freq));
}

/* Функция программного PWM для светодиода 
 * Светодиод на PC13 активный низкий (0 = горит, 1 = выкл)
 */
static void PWM_LED(uint8_t duty)
{
  /* Светодиод ГОРИТ на duty% периода (низкий уровень) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
  Delay_us(duty * 10);  /* 10 мкс на 1% duty cycle */
  
  /* Светодиод ВЫКЛЮЧЕН на (100-duty)% периода (высокий уровень) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
  Delay_us((100 - duty) * 10);
}

/* Функция плавного изменения яркости */
static void Update_PWM(void)
{
  if (pwm_direction == 0)
  {
    pwm_duty++;
    if (pwm_duty >= 100)
    {
      pwm_duty = 100;
      pwm_direction = 1;  /* Меняем направление на спад */
    }
  }
  else
  {
    pwm_duty--;
    if (pwm_duty == 0)
    {
      pwm_duty = 0;
      pwm_direction = 0;  /* Меняем направление на рост */
    }
  }
}

/* Простая задержка для антидребезга без HAL_Delay */
static void Delay_ms_simple(uint32_t ms)
{
  for (uint32_t i = 0; i < ms * 8000; i++)  /* Примерно 1 мс при 8 MHz */
  {
    __NOP();
  }
}

/* Обработчик прерывания от кнопки (вызывается из stm32f4xx_it.c) */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == BTN_Pin)  /* Кнопка на PA0 */
  {
    /* Антидребезг */
    Delay_ms_simple(20);

    /* Проверяем, что кнопка всё ещё нажата (активный низкий уровень) */
    if (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_RESET)
    {
      /* Переключаем режим PWM */
      led_pwm_active = !led_pwm_active;
    }
  }
}

/* Расчет CRC-8 для данных */
static uint8_t SPI_Calculate_CRC8(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0x00;  // Начальное значение
  
  for (uint8_t i = 0; i < length; i++)
  {
    crc ^= data[i];
    
    for (uint8_t bit = 0; bit < 8; bit++)
    {
      if (crc & 0x80)
      {
        crc = (crc << 1) ^ 0x07;
      }
      else
      {
        crc <<= 1;
      }
    }
  }
  
  return crc;
}

/* Проверка CRC-8 */
static uint8_t SPI_Validate_CRC8(const uint8_t *data, uint8_t length, uint8_t crc)
{
  return (SPI_Calculate_CRC8(data, length) == crc) ? 1 : 0;
}

/* Функция обмена данными через SPI (Master → Slave) с проверкой CRC
 * Master передаёт led_pwm_active (0 или 1) + CRC
 * Slave принимает данные, проверяет CRC и отправляет ответ
 */
static void SPI_Exchange(void)
{
  // Формируем данные для передачи
  tx_byte = led_pwm_active ? 0x01 : 0x00;
  
  // Рассчитываем CRC для данных
  tx_crc = SPI_Calculate_CRC8(&tx_byte, 1);
  
  // Master: начинаем передачу
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);  // NSS низкий
  
  // Master: передаем данные
  HAL_SPI_Transmit(&hspi1, &tx_byte, 1, 100);
  
  // Master: передаем CRC
  HAL_SPI_Transmit(&hspi1, &tx_crc, 1, 100);
  
  // Master: поднимаем NSS
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  
  // Slave: начинаем прием через прерывание
  HAL_SPI_Receive_IT(&hspi2, (uint8_t*)&slave_rx, 1);
  
  // Ждем завершения приема на Slave
  for (volatile int i = 0; i < 1000; i++);
  
  // Master: начинаем прием ответа
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
  
  // Master: принимаем данные
  HAL_SPI_Receive(&hspi1, &rx_byte, 1, 100);
  
  // Master: принимаем CRC
  HAL_SPI_Receive(&hspi1, &rx_crc, 1, 100);
  
  // Master: поднимаем NSS
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  
  // Проверяем CRC ответа
  if (SPI_Validate_CRC8(&rx_byte, 1, rx_crc))
  {
    // Данные корректны
    slave_rx = rx_byte;
    spi_crc_error = 0;
  }
  else
  {
    // Ошибка CRC
    spi_crc_error = 1;
    spi_total_errors++;
    slave_rx = 0xFF;  // Код ошибки
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */
  /* Инициализация DWT для задержек в микросекундах */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* Инициализация переменных PWM - по умолчанию светодиод выключен */
  led_pwm_active = 0;
  pwm_duty = 0;
  pwm_direction = 0;

  /* Инициализация переменных CRC */
  spi_crc_error = 0;
  spi_total_errors = 0;
  tx_crc = 0;
  rx_crc = 0;

  /* Выключаем светодиод (инверсный выход: SET = 1 = не горит) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* SPI обмен только при изменении PWM состояния */
    if (led_pwm_active != last_pwm_state)
    {
      last_pwm_state = led_pwm_active;
      SPI_Exchange();
      
      /* Проверяем ошибки после обмена */
      if (spi_crc_error)
      {
        /* Можно добавить обработку ошибки здесь */
        /* Например, повторная передача или логирование */
      }
    }

    if (led_pwm_active)
    {
      /* PWM режим: светодиод мигает с изменяемой яркостью */
      PWM_LED(pwm_duty);
      Update_PWM();
    }
    else
    {
      /* LED выключен (инверсный выход: SET = 1 = не горит) */
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 214;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enables the Clock Security System
  */
  HAL_RCC_EnableCSS();
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_SLAVE;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_Pin */
  GPIO_InitStruct.Pin = BTN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
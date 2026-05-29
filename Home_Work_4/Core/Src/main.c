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
#include <string.h>
#include "stm32f4xx_hal_uart.h"
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

UART_HandleTypeDef huart1;

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

/* Переменные для UART обработки */
#define UART_RX_BUFFER_SIZE 64
volatile uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];  /* Кольцевой буфер приема */
volatile uint16_t uart_rx_head = 0;  /* Индекс головы буфера */
volatile uint16_t uart_rx_tail = 0;  /* Индекс хвоста буфера */
uint8_t uart_rx_byte = 0;   /* Байт для приема через прерывание */
volatile uint8_t command_ready = 0;  /* Флаг готовности команды */
char command_buffer[32];            /* Буфер для сформированной команды */
volatile uint8_t send_ok_pending = 0;  /* Флаг ожидания завершения отправки OK */

/* Переменные для обработки кнопки (длительное нажатие) */
volatile uint32_t btn_press_time = 0;    /* Время нажатия кнопки (тики HAL_GetTick) */
uint8_t btn_state = 0;                   /* Состояние кнопки: 0-ожидание, 1-нажато, 2-моргание */
uint8_t long_press_active = 0;           /* Флаг выполнения моргания */
uint8_t blink_count = 0;                 /* Счетчик морганий (0-6 для 3 циклов) */
uint32_t blink_timer = 0;                /* Таймер для морганий */
uint8_t long_press_cmd_toggle = 0;       /* Тоггл для команд: 0=следующая ON, 1=следующая OFF */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void Process_UART_Commands(void);
void UART_Send_String(const char* str);
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
      /* Засекаем время нажатия */
      btn_press_time = HAL_GetTick();
      btn_state = 1;  /* Кнопка нажата, ждём отпускания */
    }
  }
}

/* Функция обмена данными через SPI (Master → Slave)
 * Master передаёт led_pwm_active (0 или 1)
 * Slave принимает в slave_rx через прерывание
 */
static void SPI_Exchange(void)
{
  tx_byte = led_pwm_active ? 0x01 : 0x00;
  slave_rx = 0xAA;  /* Паттерн для проверки (0b10101010) */
  
  /* Slave (SPI2): запускаем приём через прерывание */
  HAL_SPI_Receive_IT(&hspi2, (uint8_t*)&slave_rx, 1);
  
  /* Даём Slave время подготовиться */
  for (volatile int i = 0; i < 1000; i++);
  
  /* Master (SPI1): опускаем NSS (PA4) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
  
  /* Master передаёт данные */
  HAL_SPI_Transmit(&hspi1, &tx_byte, 1, 100);
  
  /* Master: поднимаем NSS */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  
  /* Ждём пока прерывание обработает */
  for (volatile int i = 0; i < 100000; i++);
  
  rx_byte = slave_rx;  /* Копируем для отладки */
}

/* ========== UART обработка ========== */

/* Callback: прием одного байта завершен */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    /* Сохраняем байт в кольцевой буфер */
    uint16_t next_head = (uart_rx_head + 1) % UART_RX_BUFFER_SIZE;
    if (next_head != uart_rx_tail)  /* Не перезаписываем если буфер полон */
    {
      uart_rx_buffer[uart_rx_head] = uart_rx_byte;
      uart_rx_head = next_head;
    }
    
    /* Запускаем прием следующего байта */
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
  }
}

/* Callback: отправка завершена */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    send_ok_pending = 0;
  }
}

/* Функция отправки строки через UART (асинхронно) */
void UART_Send_String(const char* str)
{
  if (send_ok_pending == 0)
  {
    HAL_UART_Transmit_IT(&huart1, (uint8_t*)str, strlen(str));
    send_ok_pending = 1;
  }
}

/* Обработка принятых команд из буфера */
void Process_UART_Commands(void)
{
  static uint16_t local_tail = 0;
  uint16_t head = uart_rx_head;
  uint16_t tail = local_tail;
  uint8_t cmd_len = 0;
  
  /* Проверяем есть ли данные в буфере */
  if (head == tail)
    return;
  
  /* Ищем конец команды: '\n' или '\r' */
  while (tail != head)
  {
    char c = (char)uart_rx_buffer[tail];
    tail = (tail + 1) % UART_RX_BUFFER_SIZE;
    
    /* Пропускаем CR и LF в начале */
    if (c == '\r' || c == '\n')
    {
      if (cmd_len > 0)
      {
        command_buffer[cmd_len] = '\0';
        
        /* Проверяем команду */
        if (strcmp(command_buffer, "AT+LED_ON") == 0)
        {
          led_pwm_active = 1;
          command_ready = 1;  /* Устанавливаем флаг получения команды */
          UART_Send_String("OK\r\n");
        }
        else if (strcmp(command_buffer, "AT+LED_OFF") == 0)
        {
          led_pwm_active = 0;
          command_ready = 1;  /* Устанавливаем флаг получения команды */
          UART_Send_String("OK\r\n");
        }
        /* Можно добавить другие AT-команды */
        
        cmd_len = 0;
        /* Очищаем буфер до следующей команды */
        local_tail = tail;
      }
      continue;
    }
    
    /* Сохраняем символ в команду */
    if (cmd_len < sizeof(command_buffer) - 1)
    {
      command_buffer[cmd_len++] = c;
    }
    else
    {
      /* Буфер переполнен, сбрасываем */
      cmd_len = 0;
    }
  }
  
  /* Обновляем глобальный tail если обработали данные */
  if (local_tail != uart_rx_tail)
  {
    uart_rx_tail = local_tail;
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
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  /* Инициализация DWT для задержек в микросекундах */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* Инициализация переменных PWM - по умолчанию светодиод выключен */
  led_pwm_active = 0;
  pwm_duty = 0;
  pwm_direction = 0;

  /* Выключаем светодиод (инверсный выход: SET = 1 = не горит) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /* Запуск асинхронного приема UART */
  HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Обработка UART команд */
    Process_UART_Commands();

    /* Обработка кнопки: определение типа нажатия */
    if (btn_state == 1 && HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_SET)
    {
      /* Кнопка отпущена */
      uint32_t press_duration = HAL_GetTick() - btn_press_time;
      
      if (press_duration >= 500)
      {
        /* Длительное нажатие: запускаем моргание */
        long_press_active = 1;
        blink_count = 0;
        blink_timer = HAL_GetTick();
        btn_state = 2;  /* Состояние моргания */
      }
      else
      {
        /* Короткое нажатие: переключаем LED */
        led_pwm_active = !led_pwm_active;
        btn_state = 0;  /* Возврат в ожидание */
      }
    }

    /* Конечный автомат моргания (3 цикла по 500мс вкл/выкл) */
    if (long_press_active && btn_state == 2)
    {
      uint32_t current_time = HAL_GetTick();
      uint32_t elapsed = current_time - blink_timer;
      
      if (elapsed >= 500)
      {
        /* Меняем состояние каждые 500мс */
        if (blink_count % 2 == 0)
        {
          /* Включить LED */
          HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        }
        else
        {
          /* Выключить LED */
          HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }
        
        blink_count++;
        blink_timer = current_time;
        
        /* После 6 переключений (3 полных цикла) завершаем */
        if (blink_count >= 6)
        {
          /* Отправляем команду на основе long_press_cmd_toggle (независимо от led_pwm_active) */
          if (long_press_cmd_toggle == 0)
          {
            UART_Send_String("AT+LED_ON\r\n");
            long_press_cmd_toggle = 1;  /* Следующая будет OFF */
          }
          else
          {
            UART_Send_String("AT+LED_OFF\r\n");
            long_press_cmd_toggle = 0;  /* Следующая будет ON */
          }
          
          long_press_active = 0;
          btn_state = 0;
        }
      }
    }

    /* Прерывание моргания при получении UART команды */
    if (long_press_active && command_ready)
    {
      long_press_active = 0;
      btn_state = 0;
      command_ready = 0;  /* Сбрасываем флаг после обработки */
      /* LED управляется через led_pwm_active в Process_UART_Commands */
    }

    /* SPI обмен только при изменении PWM состояния */
    if (led_pwm_active != last_pwm_state)
    {
      last_pwm_state = led_pwm_active;
      SPI_Exchange();
    }

    /* Управление LED в зависимости от состояния */
    if (led_pwm_active && !long_press_active)
    {
      /* PWM режим: светодиод мигает с изменяемой яркостью */
      PWM_LED(pwm_duty);
      Update_PWM();
    }
    else if (!long_press_active)
    {
      /* LED выключен (инверсный выход: SET = 1 = не горит) */
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
    /* При long_press_active управление LED выполняется в конечном автомате выше */
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
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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

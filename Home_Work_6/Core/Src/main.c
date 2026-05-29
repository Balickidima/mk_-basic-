/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Основная программа управления светодиодом
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
#include <stdio.h>
#include <stdlib.h>
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
ADC_HandleTypeDef hadc1;

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* Переменные для управления светодиодом и PWM */
volatile uint8_t svetodiod_status = 0;    /* Флаг активного PWM режима: 0 - выкл, 1 - вкл */
uint8_t pwm_current_value = 0;             /* Текущее значение duty cycle PWM (0-100) */
uint8_t pwm_direction_flag = 0;            /* Направление изменения PWM: 0 - рост, 1 - спад */

/* Переменные для SPI обмена */
uint8_t spi_data_out = 0;                  /* Байт для передачи (состояние PWM) */
volatile uint8_t spi_data_in = 0;          /* Принятый байт на Master (от Slave) */
volatile uint8_t slave_data_received = 0;  /* Принятый байт на Slave */
uint8_t previous_pwm_status = 0;          /* Для отслеживания изменения PWM */

/* Переменные для UART обработки */
#define UART_BUFFER_CAPACITY 128
volatile uint8_t uart_receive_buffer[UART_BUFFER_CAPACITY];  /* Кольцевой буфер приема */
volatile uint16_t uart_buffer_head = 0;     /* Индекс головы буфера */
volatile uint16_t uart_buffer_tail = 0;     /* Индекс хвоста буфера */
uint8_t uart_current_byte = 0;             /* Байт для приема через прерывание */
volatile uint8_t command_processed_flag = 0; /* Флаг готовности команды */
char uart_command_buffer[32];              /* Буфер для сформированной команды */
volatile uint8_t transmission_pending_flag = 0; /* Флаг ожидания завершения отправки OK */
volatile uint32_t last_receive_timestamp = 0; /* Время последнего приема байта (тики HAL_GetTick) */

/* Переменные для обработки кнопки (длительное нажатие) */
volatile uint32_t knopka_press_time = 0;   /* Время нажатия кнопки (тики HAL_GetTick) */
uint8_t knopka_current_state = 0;         /* Состояние кнопки: 0-ожидание, 1-нажато, 2-моргание */
uint8_t long_press_mode = 0;               /* Флаг выполнения моргания */
uint8_t blink_counter = 0;                 /* Счетчик морганий (0-6 для 3 циклов) */
uint32_t blink_timestamp = 0;              /* Таймер для морганий */
uint8_t command_sequence_toggle = 0;        /* Тоггл для команд: 0=следующая ON, 1=следующая OFF */

/* Переменные для ADC */
uint16_t adc_channel1_value = 0;           /* Значение ADC с PA1 */
uint16_t adc_channel2_value = 0;           /* Значение ADC с PA2 */
int16_t adc_voltage_difference = 0;        /* Разность напряжений (PA1 - PA2) */
uint8_t ambient_light_percent = 0;        /* Уровень освещённости (0-100%) */

/* Переменные для калибровки ADC */
int16_t calibration_min_value = -2000;      /* Нижняя граница (значение diff ADC) */
int16_t calibration_max_value = 600;      /* Верхняя граница (значение diff ADC) */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void system_clock_setup(void);
static void gpio_configuration(void);
static void spi1_master_setup(void);
static void spi2_slave_setup(void);
static void uart1_configuration(void);
static void adc1_setup(void);
/* USER CODE BEGIN PFP */
void uart_command_parser(void);
void uart_transmit_message(const char* str);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ========== ЗАДЕРЖКИ ========== */

/* Задержка в микросекундах для PWM */
static void microsecond_delay(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t freq = HAL_RCC_GetHCLKFreq() / 100000;  /* Частота в МГц */
  while ((DWT->CYCCNT - start) < (us * freq));
}

/* Простая задержка для антидребезга кнопки без HAL_Delay */
static void simple_button_delay(uint32_t ms)
{
  for (uint32_t i = 0; i < ms * 8000; i++)  /* Примерно 1 мс при 8 MHz */
  {
    __NOP();
  }
}

/* ========== УПРАВЛЕНИЕ СВЕТОДИОДОМ ========== */

/* Функция программного PWM для светодиода 
 * Светодиод на PC13 активный низкий (0 = горит, 1 = выкл)
 */
static void svetodiod_pwm_control(uint8_t duty)
{
  /* Светодиод ГОРИТ на duty% периода (низкий уровень) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
  microsecond_delay(duty * 10);  /* 10 мкс на 1% duty cycle */
  
  /* Светодиод ВЫКЛЮЧЕН на (100-duty)% периода (высокий уровень) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
  microsecond_delay((100 - duty) * 10);
}

/* Функция плавного изменения яркости */
static void pwm_brightness_update(void)
{
  if (pwm_direction_flag == 0)
  {
    pwm_current_value++;
    if (pwm_current_value >= 100)
    {
      pwm_current_value = 100;
      pwm_direction_flag = 1;  /* Меняем направление на спад */
    }
  }
  else
  {
    pwm_current_value--;
    if (pwm_current_value == 0)
    {
      pwm_current_value = 0;
      pwm_direction_flag = 0;  /* Меняем направление на рост */
    }
  }
}

/* ========== ОБРАБОТКА КНОПКИ ========== */

/* Обработчик прерывания от кнопки (вызывается из stm32f4xx_it.c) */
void knopka_interrupt_handler(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == BTN_Pin)  /* Кнопка на PA0 */
  {
    /* Антидребезг */
    simple_button_delay(20);

    /* Проверяем, что кнопка всё ещё нажата (активный низкий уровень) */
    if (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_RESET)
    {
      /* Засекаем время нажатия */
      knopka_press_time = HAL_GetTick();
      knopka_current_state = 1;  /* Кнопка нажата, ждём отпускания */
    }
  }
}

/* ========== ОБМЕН ДАННЫМИ ПО SPI ========== */

/* Функция обмена данными через SPI (Master -> Slave)
 * Master передаёт svetodiod_status (0 или 1)
 * Slave принимает в slave_data_received через прерывание
 */
static void spi_data_transfer(void)
{
  spi_data_out = svetodiod_status ? 0x01 : 0x00;
  slave_data_received = 0xAA;  /* Паттерн для проверки (0b10101010) */
  
  /* Slave (SPI2): запускаем приём через прерывание */
  HAL_SPI_Receive_IT(&hspi2, (uint8_t*)&slave_data_received, 1);
  
  /* Даём Slave время подготовиться */
  for (volatile int i = 0; i < 1000; i++);
  
  /* Master (SPI1): опускаем NSS (PA4) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
  
  /* Master передаёт данные */
  HAL_SPI_Transmit(&hspi1, &spi_data_out, 1, 100);
  
  /* Master: поднимаем NSS */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  
  /* Ждём пока прерывание обработает */
  for (volatile int i = 0; i < 100000; i++);
  
  spi_data_in = slave_data_received;  /* Копируем для отладки */
}

/* ========== ОБРАБОТКА UART ========== */

/* Callback: прием одного байта завершен */
void uart_receive_complete_handler(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    /* Сохраняем байт в кольцевой буфер */
    uint16_t next_head = (uart_buffer_head + 1) % UART_BUFFER_CAPACITY;
    if (next_head != uart_buffer_tail)  /* Не перезаписываем если буфер полон */
    {
      uart_receive_buffer[uart_buffer_head] = uart_current_byte;
      uart_buffer_head = next_head;
    }
    
    /* Обновляем время последнего приема */
    last_receive_timestamp = HAL_GetTick();
    
    /* Запускаем прием следующего байта */
    HAL_UART_Receive_IT(&huart1, &uart_current_byte, 1);
  }
}

/* Callback: отправка завершена */
void uart_transmit_complete_handler(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    transmission_pending_flag = 0;
  }
}

/* Функция отправки строки через UART (асинхронно) */
void uart_transmit_message(const char* str)
{
  if (transmission_pending_flag == 0)
  {
    HAL_UART_Transmit_IT(&huart1, (uint8_t*)str, strlen(str));
    transmission_pending_flag = 1;
  }
}

/* ========== ЧТЕНИЕ ADC ========== */

/* Функция чтения значений ADC с PA1 и PA2 */
static void adc_sensor_read(void)
{
  /* Запускаем ADC */
  HAL_ADC_Start(&hadc1);
  
  /* Читаем канал 1 (PA1) */
  HAL_ADC_PollForConversion(&hadc1, 10);
  adc_channel1_value = HAL_ADC_GetValue(&hadc1);
  
  /* Читаем канал 2 (PA2) */
  HAL_ADC_PollForConversion(&hadc1, 10);
  adc_channel2_value = HAL_ADC_GetValue(&hadc1);
  
  /* Останавливаем ADC */
  HAL_ADC_Stop(&hadc1);
  
  /* Вычисляем разность напряжений */
  adc_voltage_difference = (int16_t)adc_channel1_value - (int16_t)adc_channel2_value;
  
  /* Преобразуем в уровень освещённости (0-100%) */
  /* Используем калибровочные границы */
  
  if (adc_voltage_difference < calibration_min_value) {
    ambient_light_percent = 0;  /* Очень ярко - светодиод выключен */
  } else if (adc_voltage_difference > calibration_max_value) {
    ambient_light_percent = 100; /* Очень темно - светодиод на максимум */
  } else {
    /* Линейное преобразование */
    int32_t range = calibration_max_value - calibration_min_value;
    ambient_light_percent = (uint8_t)(((int32_t)(adc_voltage_difference - calibration_min_value) * 100) / range);
  }
}

/* ========== РАЗБОР КОМАНД UART ========== */

/* Обработка принятых команд из буфера */
void uart_command_parser(void)
{
  uint16_t head = uart_buffer_head;
  uint16_t tail = uart_buffer_tail;
  uint8_t cmd_len = 0;
  uint16_t processed_tail = tail;  /* Локальная переменная для отслеживания позиции */
  
  /* Проверяем есть ли данные в буфере */
  if (head == tail)
    return;
  
  /* Ищем конец команды: '\n' или '\r' */
  while (processed_tail != head)
  {
    char c = (char)uart_receive_buffer[processed_tail];
    processed_tail = (processed_tail + 1) % UART_BUFFER_CAPACITY;
    
    /* Пропускаем CR и LF в начале */
    if (c == '\r' || c == '\n')
    {
      if (cmd_len > 0)
      {
        uart_command_buffer[cmd_len] = '\0';
        
        /* Проверяем команду */
        if (strcmp(uart_command_buffer, "AT+LED_ON") == 0)
        {
          svetodiod_status = 1;
          command_processed_flag = 1;  /* Устанавливаем флаг получения команды */
          uart_transmit_message("OK\r\n");
        }
        else if (strcmp(uart_command_buffer, "AT+LED_OFF") == 0)
        {
          svetodiod_status = 0;
          command_processed_flag = 1;  /* Устанавливаем флаг получения команды */
          uart_transmit_message("OK\r\n");
        }
        /* Команды для ADC */
        else if (strcmp(uart_command_buffer, "AT+ADC_RAW?") == 0)
        {
          /* Читаем ADC и возвращаем сырое значение */
          adc_sensor_read();
          char response[32];
          sprintf(response, "RAW:%hd\r\n", adc_voltage_difference);
          uart_transmit_message(response);
        }
        else if (strcmp(uart_command_buffer, "AT+ADC_PERCENT?") == 0)
        {
          /* Читаем ADC и возвращаем процент */
          adc_sensor_read();
          char response[32];
          sprintf(response, "PERCENT:%d\r\n", ambient_light_percent);
          uart_transmit_message(response);
        }
        else if (strncmp(uart_command_buffer, "AT+CALIB_LOW+", 13) == 0)
        {
          /* Увеличиваем нижнюю границу */
          int16_t value = atoi(&uart_command_buffer[13]);
          calibration_min_value += value;
          char response[32];
          sprintf(response, "OK LOW:%hd\r\n", calibration_min_value);
          uart_transmit_message(response);
        }
        else if (strncmp(uart_command_buffer, "AT+CALIB_LOW-", 13) == 0)
        {
          /* Уменьшаем нижнюю границу */
          int16_t value = atoi(&uart_command_buffer[13]);
          calibration_min_value -= value;
          char response[32];
          sprintf(response, "OK LOW:%hd\r\n", calibration_min_value);
          uart_transmit_message(response);
        }
        else if (strncmp(uart_command_buffer, "AT+CALIB_HIGH+", 14) == 0)
        {
          /* Увеличиваем верхнюю границу */
          int16_t value = atoi(&uart_command_buffer[14]);
          calibration_max_value += value;
          char response[32];
          sprintf(response, "OK HIGH:%hd\r\n", calibration_max_value);
          uart_transmit_message(response);
        }
        else if (strncmp(uart_command_buffer, "AT+CALIB_HIGH-", 14) == 0)
        {
          /* Уменьшаем верхнюю границу */
          int16_t value = atoi(&uart_command_buffer[14]);
          calibration_max_value -= value;
          char response[32];
          sprintf(response, "OK HIGH:%hd\r\n", calibration_max_value);
          uart_transmit_message(response);
        }
        /* Можно добавить другие AT-команды */
        
        cmd_len = 0;
      }
      /* Обновляем глобальный tail - пропускаем терминатор */
      uart_buffer_tail = processed_tail;
      continue;
    }
    
    /* Сохраняем символ в команду */
    if (cmd_len < sizeof(uart_command_buffer) - 1)
    {
      uart_command_buffer[cmd_len++] = c;
    }
    else
    {
      /* Буфер переполнен, сбрасываем */
      cmd_len = 0;
    }
  }
  
  /* Если нашли терминатор, uart_buffer_tail уже обновлён выше.
     Если терминатор не найден, данные остаются в буфере для таймаутной обработки. */
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
  system_clock_setup();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  gpio_configuration();
  spi1_master_setup();
  spi2_slave_setup();
  uart1_configuration();
  adc1_setup();
  /* USER CODE BEGIN 2 */
  /* Инициализация DWT для задержек в микросекундах */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* Инициализация переменных PWM - по умолчанию светодиод выключен */
  svetodiod_status = 0;
  pwm_current_value = 0;
  pwm_direction_flag = 0;

  /* Выключаем светодиод (инверсный выход: SET = 1 = не горит) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /* Запуск асинхронного приема UART */
  HAL_UART_Receive_IT(&huart1, &uart_current_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Обработка UART команд */
    uart_command_parser();
    
    /* Обработка команд по таймауту (если нет CR/LF) */
    if (uart_buffer_head != uart_buffer_tail && (HAL_GetTick() - last_receive_timestamp) > 100)
    {
      /* Есть данные в буфере и прошло 100мс с последнего приёма */
      /* Обрабатываем как команду без CR/LF */
      uint16_t head = uart_buffer_head;
      uint16_t tail = uart_buffer_tail;
      uint8_t cmd_len = 0;
      
      while (tail != head && cmd_len < sizeof(uart_command_buffer) - 1)
      {
        uart_command_buffer[cmd_len++] = (char)uart_receive_buffer[tail];
        tail = (tail + 1) % UART_BUFFER_CAPACITY;
      }
      uart_command_buffer[cmd_len] = '\0';
      
      /* Очищаем команду от пробелов и CR/LF в конце */
      while (cmd_len > 0 && (uart_command_buffer[cmd_len-1] == ' ' || 
                              uart_command_buffer[cmd_len-1] == '\r' || 
                              uart_command_buffer[cmd_len-1] == '\n'))
      {
        uart_command_buffer[--cmd_len] = '\0';
      }
      
      /* Очищаем команду от пробелов в начале */
      char *cmd_start = uart_command_buffer;
      while (*cmd_start == ' ') cmd_start++;
      
      /* Очищаем весь буфер после обработки команды */
      uart_buffer_tail = uart_buffer_head;
      
      /* Сбрасываем время последнего приёма чтобы не обрабатывать ту же команду снова */
      last_receive_timestamp = HAL_GetTick();
      
      /* Проверяем команду */
      if (strcmp(cmd_start, "AT+LED_ON") == 0)
      {
        svetodiod_status = 1;
        command_processed_flag = 1;
        uart_transmit_message("OK\r\n");
      }
      else if (strcmp(cmd_start, "AT+LED_OFF") == 0)
      {
        svetodiod_status = 0;
        command_processed_flag = 1;
        uart_transmit_message("OK\r\n");
      }
      else if (strcmp(cmd_start, "AT+ADC_RAW?") == 0)
      {
        adc_sensor_read();
        char response[32];
        sprintf(response, "RAW:%hd\r\n", adc_voltage_difference);
        uart_transmit_message(response);
      }
      else if (strcmp(cmd_start, "AT+ADC_PERCENT?") == 0)
      {
        adc_sensor_read();
        char response[32];
        sprintf(response, "PERCENT:%d\r\n", ambient_light_percent);
        uart_transmit_message(response);
      }
      else if (strncmp(cmd_start, "AT+CALIB_LOW+", 13) == 0)
      {
        int16_t value = atoi(&cmd_start[13]);
        calibration_min_value += value;
        char response[32];
        sprintf(response, "OK LOW:%hd\r\n", calibration_min_value);
        uart_transmit_message(response);
      }
      else if (strncmp(cmd_start, "AT+CALIB_LOW-", 13) == 0)
      {
        int16_t value = atoi(&cmd_start[13]);
        calibration_min_value -= value;
        char response[32];
        sprintf(response, "OK LOW:%hd\r\n", calibration_min_value);
        uart_transmit_message(response);
      }
      else if (strncmp(cmd_start, "AT+CALIB_HIGH+", 14) == 0)
      {
        int16_t value = atoi(&cmd_start[14]);
        calibration_max_value += value;
        char response[32];
        sprintf(response, "OK HIGH:%hd\r\n", calibration_max_value);
        uart_transmit_message(response);
      }
      else if (strncmp(cmd_start, "AT+CALIB_HIGH-", 14) == 0)
      {
        int16_t value = atoi(&cmd_start[14]);
        calibration_max_value -= value;
        char response[32];
        sprintf(response, "OK HIGH:%hd\r\n", calibration_max_value);
        uart_transmit_message(response);
      }
    }

    /* Обработка кнопки: определение типа нажатия */
    if (knopka_current_state == 1 && HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_SET)
    {
      /* Кнопка отпущена */
      uint32_t press_duration = HAL_GetTick() - knopka_press_time;
      
      if (press_duration >= 500)
      {
        /* Длительное нажатие: запускаем моргание */
        long_press_mode = 1;
        blink_counter = 0;
        blink_timestamp = HAL_GetTick();
        knopka_current_state = 2;  /* Состояние моргания */
      }
      else
      {
        /* Короткое нажатие: переключаем LED */
        svetodiod_status = !svetodiod_status;
        knopka_current_state = 0;  /* Возврат в ожидание */
      }
    }

    /* Конечный автомат моргания (3 цикла по 500мс вкл/выкл) */
    if (long_press_mode && knopka_current_state == 2)
    {
      uint32_t current_time = HAL_GetTick();
      uint32_t elapsed = current_time - blink_timestamp;
      
      if (elapsed >= 500)
      {
        /* Меняем состояние каждые 500мс */
        if (blink_counter % 2 == 0)
        {
          /* Включить LED */
          HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        }
        else
        {
          /* Выключить LED */
          HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }
        
        blink_counter++;
        blink_timestamp = current_time;
        
        /* После 6 переключений (3 полных цикла) завершаем */
        if (blink_counter >= 6)
        {
          /* Отправляем команду на основе command_sequence_toggle (независимо от svetodiod_status) */
          if (command_sequence_toggle == 0)
          {
            uart_transmit_message("AT+LED_ON\r\n");
            command_sequence_toggle = 1;  /* Следующая будет OFF */
          }
          else
          {
            uart_transmit_message("AT+LED_OFF\r\n");
            command_sequence_toggle = 0;  /* Следующая будет ON */
          }
          
          long_press_mode = 0;
          knopka_current_state = 0;
        }
      }
    }

    /* Прерывание моргания при получении UART команды */
    if (long_press_mode && command_processed_flag)
    {
      long_press_mode = 0;
      knopka_current_state = 0;
      command_processed_flag = 0;  /* Сбрасываем флаг после обработки */
      /* LED управляется через svetodiod_status в uart_command_parser */
    }

    /* SPI обмен только при изменении PWM состояния */
    if (svetodiod_status != previous_pwm_status)
    {
      previous_pwm_status = svetodiod_status;
      spi_data_transfer();
    }

    /* Управление LED в зависимости от состояния */
    if (svetodiod_status && !long_press_mode)
    {
      /* Читаем ADC и обновляем яркость на основе освещённости */
      adc_sensor_read();
      
      /* Устанавливаем яркость на основе уровня освещённости */
      pwm_current_value = ambient_light_percent;
      
      /* PWM режим: светодиод с яркостью зависящей от освещённости */
      svetodiod_pwm_control(pwm_current_value);
    }
    else if (!long_press_mode)
    {
      /* LED выключен (инверсный выход: SET = 1 = не горит) */
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
    /* При long_press_mode управление LED выполняется в конечном автомате выше */
    
    /* Сброс флага command_processed_flag после обработки команды */
    command_processed_flag = 0;
  }
  /* USER CODE END 3 */
}

/**
  * @brief Настройка системных часов
  * @retval None
  */
void system_clock_setup(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Настройка выходного напряжения внутреннего регулятора
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Инициализация RCC генераторов согласно указанным параметрам
  * в структуре RCC_OscInitTypeDef.
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
    critical_error_handler();
  }

  /** Инициализация тактовых частот CPU, AHB и APB шин
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    critical_error_handler();
  }

  /** Включение системы безопасности часов
  */
  HAL_RCC_EnableCSS();
}

/**
  * @brief Инициализация ADC1
  * @param None
  * @retval None
  */
static void adc1_setup(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Конфигурация глобальных параметров ADC (тактирование, разрешение, выравнивание данных и количество преобразований)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    critical_error_handler();
  }

  /** Конфигурация выбранного регулярного канала ADC: его ранг в последовательности и время выборки
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_112CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    critical_error_handler();
  }

  /** Конфигурация выбранного регулярного канала ADC: его ранг в последовательности и время выборки
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    critical_error_handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief Инициализация SPI1
  * @param None
  * @retval None
  */
static void spi1_master_setup(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* Конфигурация параметров SPI1*/
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
    critical_error_handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief Инициализация SPI2
  * @param None
  * @retval None
  */
static void spi2_slave_setup(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* Конфигурация параметров SPI2*/
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
    critical_error_handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief Инициализация USART1
  * @param None
  * @retval None
  */
static void uart1_configuration(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    critical_error_handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief Инициализация GPIO
  * @param None
  * @retval None
  */
static void gpio_configuration(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* Включение тактирования GPIO портов */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Настройка уровня вывода GPIO */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Настройка GPIO вывода : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Настройка GPIO вывода : BTN_Pin */
  GPIO_InitStruct.Pin = BTN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_GPIO_Port, &GPIO_InitStruct);

  /* Инициализация прерываний EXTI*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Эта функция выполняется при возникновении ошибки
  * @retval None
  */
void critical_error_handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Пользователь может добавить свою реализацию для отчета о состоянии ошибки HAL */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Сообщает имя исходного файла и номер строки, где произошла ошибка assert_param
  * @param  file: указатель на имя исходного файла
  * @param  line: номер строки с ошибкой в исходном файле
  * @retval None
  */
void assertion_failure_handler(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* Пользователь может добавить свою реализацию для вывода имени файла и номера строки,
     например: printf("Неправильные параметры: файл %s на строке %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : DMA transfer benchmark - 256 bytes M2IO and M2M
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <string.h>
#include <stdio.h>

#define MAX_SIZE 4096

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart2_tx;
DMA_HandleTypeDef hdma_memtomem;

static uint8_t src[MAX_SIZE];
static uint8_t dst[MAX_SIZE];
// Background task variables
static volatile uint32_t bg_task_counter = 0;
static volatile uint8_t uart2_tx_done = 0;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_DMA_Mem2Mem_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
void Error_Handler(void);

/* USER CODE BEGIN 0 */
static void DWT_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t DWT_GetCycles(void)
{
  return DWT->CYCCNT;
}

static uint32_t cycles_to_us(uint32_t cycles)
{
  uint32_t hz = HAL_RCC_GetHCLKFreq();
  return (uint32_t)((uint64_t)cycles * 1000000ULL / (uint64_t)hz);
}

static void uart1_print(const char *s)
{
  HAL_UART_Transmit(&huart1, (uint8_t*)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

static void fill_src(uint32_t size)
{
  for (uint32_t i = 0; i < size; i++)
    src[i] = (uint8_t)(i ^ 0x5A);
}

// Background task: simple counter increment (very fast)
static void background_task(void)
{
  bg_task_counter++;
}

// Wait for DMA transfer complete using register-level polling
static void dma_wait_m2m(uint32_t tc_flag, uint32_t clear_flag)
{
  while (!(DMA1->ISR & tc_flag))
  {
    background_task();  // background task running
  }
  DMA1->IFCR = clear_flag;  // Clear flag
}

static void bench_mem2io_dma(uint32_t size, uint32_t *time_us, uint32_t *bg_tasks)
{
  bg_task_counter = 0;
  uint32_t t0 = DWT_GetCycles();

  if (HAL_UART_Transmit_DMA(&huart2, src, size) != HAL_OK)
    Error_Handler();

  // Wait for UART TC flag with background tasks
  // Note: For UART DMA, HAL_UART_Transmit_DMA uses HAL_DMA_Start_IT (interrupt-based)
  // So we need to wait for UART TC flag which is set after DMA completes and TC interrupt occurs
  while (!__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC))
  {
    background_task();
  }

  uint32_t dt = DWT_GetCycles() - t0;
  *time_us = cycles_to_us(dt);
  *bg_tasks = bg_task_counter;
}

static void bench_mem2mem_dma(uint32_t size, uint32_t *time_us, uint32_t *bg_tasks)
{
  bg_task_counter = 0;
  uint32_t t0 = DWT_GetCycles();

  // Start DMA transfer (using Start instead of Start_IT since we poll)
  if (HAL_DMA_Start(&hdma_memtomem, (uint32_t)src, (uint32_t)dst, size) != HAL_OK)
    Error_Handler();

  // Wait for completion using register-level polling with background tasks
  dma_wait_m2m(DMA_ISR_TCIF1, DMA_IFCR_CTCIF1);

  // Update DMA state and unlock (required after HAL_DMA_Start)
  hdma_memtomem.State = HAL_DMA_STATE_READY;
  __HAL_UNLOCK(&hdma_memtomem);

  uint32_t dt = DWT_GetCycles() - t0;
  *time_us = cycles_to_us(dt);
  *bg_tasks = bg_task_counter;
}

static void bench_mem2io_pio(uint32_t size, uint32_t *time_us, uint32_t *bg_tasks)
{
  uint32_t t0 = DWT_GetCycles();

  for (uint32_t i = 0; i < size; i++)
  {
    while (!__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TXE)) {}
    huart2.Instance->DR = src[i];
  }

  while (!__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC)) {}

  uint32_t dt = DWT_GetCycles() - t0;
  *time_us = cycles_to_us(dt);
  *bg_tasks = 0;
}

static void bench_mem2mem_pio(uint32_t size, uint32_t *time_us, uint32_t *bg_tasks)
{
  uint32_t t0 = DWT_GetCycles();

  for (uint32_t i = 0; i < size; i++)
  {
    dst[i] = src[i];
  }

  uint32_t dt = DWT_GetCycles() - t0;
  *time_us = cycles_to_us(dt);
  *bg_tasks = 0; // PIO doesn't allow background tasks
}

static void bench_mem2io_it(uint32_t size, uint32_t *time_us, uint32_t *bg_tasks)
{
  uint32_t bg_counter = 0;
  uart2_tx_done = 0;
  uint32_t t0 = DWT_GetCycles();

  if (HAL_UART_Transmit_IT(&huart2, src, size) != HAL_OK)
    Error_Handler();

  while (!uart2_tx_done)
  {
    bg_counter++;
  }

  uint32_t dt = DWT_GetCycles() - t0;
  *time_us = cycles_to_us(dt);
  *bg_tasks = bg_counter;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart2)
  {
    uart2_tx_done = 1;  // For IT mode
  }
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_DMA_Mem2Mem_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();

  DWT_Init();

  uart1_print("\r\n=== Transfer Benchmark ===\r\n");
  uart1_print("Testing sizes: 256, 1024, 2048, 4096 bytes\r\n");
  uart1_print("Methods: DMA, Programmed IO, and Interrupt-based\r\n\r\n");

  uint32_t sizes[4] = {256, 1024, 2048, 4096};
  uint32_t m2io_dma_times[4];
  uint32_t m2m_dma_times[4];
  uint32_t m2io_pio_times[4];
  uint32_t m2m_pio_times[4];
  uint32_t m2io_it_times[4];
  uint32_t m2io_dma_bg_tasks[4];
  uint32_t m2m_dma_bg_tasks[4];
  uint32_t m2io_it_bg_tasks[4];

  for (int i = 0; i < 4; i++)
  {
    uint32_t size = sizes[i];
    fill_src(size);

    char msg[80];
    snprintf(msg, sizeof(msg), "Testing %lu bytes...\r\n", (unsigned long)size);
    uart1_print(msg);

    bench_mem2io_dma(size, &m2io_dma_times[i], &m2io_dma_bg_tasks[i]);
    bench_mem2mem_dma(size, &m2m_dma_times[i], &m2m_dma_bg_tasks[i]);
    uint32_t dummy_pio;
    bench_mem2io_pio(size, &m2io_pio_times[i], &dummy_pio);
    bench_mem2mem_pio(size, &m2m_pio_times[i], &dummy_pio);
    bench_mem2io_it(size, &m2io_it_times[i], &m2io_it_bg_tasks[i]);
  }

  uart1_print("\r\n=== Memory to IO Results ===\r\n");
  uart1_print("Size | DMA(us) | PIO(us) | IT(us)\r\n");
  uart1_print("-----|---------|---------|---------\r\n");

  for (int i = 0; i < 4; i++)
  {
    char line[120];
    snprintf(line, sizeof(line), "%4lu | %7lu | %7lu | %7lu\r\n",
             (unsigned long)sizes[i],
             (unsigned long)m2io_dma_times[i],
             (unsigned long)m2io_pio_times[i],
             (unsigned long)m2io_it_times[i]);
    uart1_print(line);
  }

  uart1_print("\r\n=== Memory to Memory Results ===\r\n");
  uart1_print("Size | DMA(us) | PIO(us) \r\n");
  uart1_print("-----|---------|---------\r\n");

  for (int i = 0; i < 4; i++)
  {
    char line[120];
    snprintf(line, sizeof(line), "%4lu | %7lu | %7lu\r\n",
             (unsigned long)sizes[i],
             (unsigned long)m2m_dma_times[i],
             (unsigned long)m2m_pio_times[i]);
    uart1_print(line);
  }

  uart1_print("\r\n=== Done ===\r\n");

  while (1) {}
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
    Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
    Error_Handler();
}

static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  hdma_usart2_tx.Instance = DMA1_Channel7;
  hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_usart2_tx.Init.Mode = DMA_NORMAL;
  hdma_usart2_tx.Init.Priority = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK)
    Error_Handler();

  __HAL_LINKDMA(&huart2, hdmatx, hdma_usart2_tx);

  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
}

static void MX_DMA_Mem2Mem_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  hdma_memtomem.Instance = DMA1_Channel1;
  hdma_memtomem.Init.Direction = DMA_MEMORY_TO_MEMORY;
  hdma_memtomem.Init.PeriphInc = DMA_PINC_ENABLE;
  hdma_memtomem.Init.MemInc = DMA_MINC_ENABLE;
  hdma_memtomem.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_memtomem.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_memtomem.Init.Mode = DMA_NORMAL;
  hdma_memtomem.Init.Priority = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&hdma_memtomem) != HAL_OK)
    Error_Handler();

  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

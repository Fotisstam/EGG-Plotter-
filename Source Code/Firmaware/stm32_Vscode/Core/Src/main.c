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
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include <math.h>
#include <string.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PROTO_SYNC0      0xAAU
#define PROTO_SYNC1      0x55U
#define NUM_CHANNELS     32U
#define DISPLAY_BINS     128U
#define PROTO_HDR_SIZE   7U
#define PROTO_DATA_BYTES (NUM_CHANNELS * DISPLAY_BINS * 2U)
#define PROTO_FRAME_SIZE (PROTO_HDR_SIZE + PROTO_DATA_BYTES + 2U)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Frame cadence. Keep this comfortably above 2x the highest channel
   frequency below (see SINE_FREQ_MAX) to avoid aliasing between frames. */
#define FRAME_PERIOD_MS  50U

/* All channels share the same frequency/phase so they visibly move in
   lockstep. Change SINE_FREQ_STEP/PHASE_STEP back to non-zero if you
   intentionally want per-channel diversity again -- just raise
   FRAME_PERIOD_MS so the fastest channel is still safely sampled. */
#define SINE_FREQ_BASE   2.0f
#define SINE_FREQ_STEP   0.0f
#define SINE_PHASE_STEP  0.0f

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
static uint16_t g_seq = 0;

/* USER CODE BEGIN PV */

/* Double-buffered TX frames. These are ~8.2 KB each (16.4 KB total) --
   far too large for the default main stack, so they MUST be static/global,
   never a local array inside main(). */
static uint8_t frame[2][PROTO_FRAME_SIZE];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
  uint16_t crc = 0xFFFFU;

  for (uint32_t i = 0; i < len; ++i)
  {
    crc ^= (uint16_t)data[i] << 8;

    for (uint8_t bit = 0; bit < 8; ++bit)
    {
      if (crc & 0x8000U)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      }
      else
      {
        crc <<= 1;
      }
    }
  }

  return crc;
}

static void build_sine_frame(uint8_t *frame_buf)
{
  uint8_t *p = frame_buf;

  /* sync */
  *p++ = PROTO_SYNC0;
  *p++ = PROTO_SYNC1;

  uint16_t seq = g_seq++;
  memcpy(p, &seq, 2);
  p += 2;

  *p++ = (uint8_t)NUM_CHANNELS;

  uint16_t nbins = DISPLAY_BINS;
  memcpy(p, &nbins, 2);
  p += 2;

  /* p is now at PROTO_HDR_SIZE (7), an odd offset -- do NOT cast this to
     uint16_t* and dereference it directly, that's an unaligned/UB access.
     Write each sample through memcpy instead. */
  uint32_t tick = HAL_GetTick();
  const float t = (float)tick / 1000.0f;

  for (uint16_t ch = 0; ch < NUM_CHANNELS; ++ch)
  {
    const float phase = (float)ch * SINE_PHASE_STEP;
    const float freq = SINE_FREQ_BASE + (float)ch * SINE_FREQ_STEP;

    for (uint16_t bin = 0; bin < DISPLAY_BINS; ++bin)
    {
      const float sine = sinf(2.0f * (float)M_PI * (freq * t + (float)bin / 32.0f + phase));
      float mag = 20000.0f + (sine + 1.0f) * 20000.0f;

      if (mag < 0.0f) mag = 0.0f;
      if (mag > 65535.0f) mag = 65535.0f;

      uint16_t sample = (uint16_t)mag;
      memcpy(p, &sample, 2);
      p += 2;
    }
  }

  uint16_t crc = crc16_ccitt(frame_buf + PROTO_HDR_SIZE, PROTO_DATA_BYTES);
  memcpy(p, &crc, 2);
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
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint8_t frame_index = 0U;
  uint32_t last_tx = 0U;
  uint32_t last_led_toggle = 0U;
  uint32_t tx_drop_count = 0U; /* bumped whenever CDC_Transmit_FS is busy */

  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* Non-blocking transmit gate. No HAL_Delay() in this loop, so this
       check runs every iteration instead of being quantized to a fixed
       delay step -- the frame cadence now matches FRAME_PERIOD_MS. */
    if ((now - last_tx) >= FRAME_PERIOD_MS)
    {
      build_sine_frame(frame[frame_index]);

      if (CDC_Transmit_FS(frame[frame_index], PROTO_FRAME_SIZE) == USBD_OK)
      {
        frame_index ^= 1U;
        last_tx = now;
      }
      else
      {
        /* Host not keeping up / endpoint busy. Don't advance last_tx so
           we retry ASAP on the next loop iteration instead of waiting
           out a fixed delay; just track it so it's visible if it happens. */
        tx_drop_count++;
      }
    }

    /* Toggle the heartbeat LED every 20 ms without blocking the loop. */
    if ((now - last_led_toggle) >= 20U)
    {
      HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2);
      last_led_toggle = now;
    }
     
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 72;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET);

  /*Configure GPIO pin : PB2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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
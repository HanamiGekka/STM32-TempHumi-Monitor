#include "bsp_uart_command.h"

void bsp_uart_command_read_identity(uint8_t uid[12], uint16_t *flash_kib, uint16_t *device_id) {
  const uint32_t words[3]={HAL_GetUIDw0(),HAL_GetUIDw1(),HAL_GetUIDw2()};
  for(unsigned n=0;n<12U;++n) uid[n]=(uint8_t)(words[n/4U]>>(8U*(n%4U)));
  *flash_kib=*(const volatile uint16_t *)FLASHSIZE_BASE;
  /* ES096 section2.2.2: zero is expected without debug access. Keep that raw
     diagnostic honest; the always-readable factory UID above owns identity. */
  *device_id=(uint16_t)HAL_GetDEVID();
}
enum { BSP_UART_COMMAND_RING_BYTES=256, BSP_UART_COMMAND_QUEUE_OVERFLOW=1U<<16,
       BSP_UART_COMMAND_REARM_ERROR=1U<<17 };
static UART_HandleTypeDef *s_uart;
static uint8_t s_rx_byte,s_ring[BSP_UART_COMMAND_RING_BYTES];
/* Single ISR producer/main-loop consumer. Aligned uint16 accesses are atomic
   on Cortex-M3; IRQ-masked operations protect resets/fault read-clear. No TX,
   parsing, OLED, heap or blocking log operations execute inside callbacks. */
static volatile uint16_t s_head,s_tail;
static volatile uint32_t s_fault;
static volatile bool s_rearm;
HAL_StatusTypeDef bsp_uart_command_init(UART_HandleTypeDef *uart) {
  s_uart=uart; s_head=s_tail=0U; s_fault=0U; s_rearm=false;
  return HAL_UART_Receive_IT(s_uart,&s_rx_byte,1U);
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart) {
  if(uart!=s_uart) return;
  uint16_t next=(uint16_t)((s_head+1U)%BSP_UART_COMMAND_RING_BYTES);
  if(next==s_tail) s_fault|=BSP_UART_COMMAND_QUEUE_OVERFLOW;
  else { s_ring[s_head]=s_rx_byte; __DMB(); s_head=next; }
  if(HAL_UART_Receive_IT(s_uart,&s_rx_byte,1U)!=HAL_OK) {
    s_fault|=BSP_UART_COMMAND_REARM_ERROR; s_rearm=true;
  }
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart) {
  if(uart==s_uart) { s_fault|=uart->ErrorCode; s_rearm=true; }
}
uint32_t bsp_uart_command_take_fault(void) {
  uint32_t irq=__get_PRIMASK(); __disable_irq();
  uint32_t fault=s_fault; s_fault=0U;
  if(fault!=0U) s_tail=s_head;
  bool rearm=s_rearm; s_rearm=false;
  __set_PRIMASK(irq);
  if(rearm) {
    /* Abort only RX; ongoing UART TX/log traffic is independent. */
    (void)HAL_UART_AbortReceive(s_uart);
    if(HAL_UART_Receive_IT(s_uart,&s_rx_byte,1U)!=HAL_OK) {
      irq=__get_PRIMASK(); __disable_irq(); s_rearm=true;
      s_fault|=BSP_UART_COMMAND_REARM_ERROR; __set_PRIMASK(irq);
    }
  }
  return fault;
}
bool bsp_uart_command_read_byte(uint8_t *byte) {
  if(s_tail==s_head) return false;
  *byte=s_ring[s_tail]; __DMB();
  s_tail=(uint16_t)((s_tail+1U)%BSP_UART_COMMAND_RING_BYTES); return true;
}

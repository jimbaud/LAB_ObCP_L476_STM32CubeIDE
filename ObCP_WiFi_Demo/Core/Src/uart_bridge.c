/**
 ******************************************************************************
 * @file    uart_bridge.c
 * @brief   Passerelle USB <-> UART du module (voir uart_bridge.h).
 *
 *  Réception UART : DMA1 canal 6 en mode circulaire (tampon de 1 Ko), lu par
 *  la boucle. Émission UART : DMA1 canal 7 depuis un tampon circulaire de
 *  2 Ko rempli avec les octets reçus du PC. Sans interruption : la boucle
 *  ne fait rien d'autre pendant la session.
 *
 *  Fin de session : voir bridge_ctl.h (bouton, terminal rouvert après la
 *  programmation, ou silence prolongé). L'état du signal DTR seul ne suffit
 *  pas : QConn_Flash le manipule pour tenter de redémarrer la puce.
 *
 *  L'USART2 et le DMA1 ne sont pas déclarés dans CubeMX : ils ne servent que
 *  pendant la session et sont pilotés ici au niveau des registres. Dans le
 *  .ioc, PA2/PA3 (NCP_UART_TX/RX) sont des entrées avec tirage haut, leur
 *  état de repos, rétabli à la fin de la session. Ne pas activer l'USART2
 *  dans CubeMX : la broche TX piloterait l'UART du module en permanence.
 ******************************************************************************
 */
#include <string.h>
#include "uart_bridge.h"
#include "board.h"
#include "cdc_port.h"

#define RX_DMA_SIZE      1024U               /* réception : tampon circulaire du DMA */
#define TX_SIZE          2048U               /* émission : puissance de 2 */
#define BAUD_MIN         1200U
#define BAUD_MAX         5000000U            /* USART2 à 80 MHz, suréchantillonnage x16 */

/* Le DMA (« Direct Memory Access ») copie seul les octets entre l'USART2 et
 * la mémoire, sans le processeur : indispensable à 2 Mbit/s.            */
static uint8_t  s_rx_dma[RX_DMA_SIZE];       /* rempli en boucle par le DMA       */
static uint32_t s_rx_rd;                     /* position de lecture du programme  */
static uint8_t  s_tx[TX_SIZE];               /* octets du PC à envoyer au module  */
static uint32_t s_tx_head;
static uint32_t s_tx_tail;
static uint32_t s_tx_inflight;               /* octets confiés au DMA d'émission  */

/* ---- USART2 + DMA ------------------------------------------------------------- */
/* Change le débit de l'USART2 (après la fin de l'émission en cours) :
 * BRR = fréquence de l'horloge / débit, arrondi.                        */
static void uart_set_baud(uint32_t baud)
{
  uint32_t t0 = HAL_GetTick();

  if ((baud < BAUD_MIN) || (baud > BAUD_MAX))
  {
    return;
  }
  /* Attente de la fin de l'émission en cours (changement de débit entre deux phases) */
  while (((s_tx_inflight != 0U) && ((DMA1->ISR & DMA_ISR_TCIF7) == 0U)) && ((HAL_GetTick() - t0) < 50U))
  {
  }
  while (((USART2->ISR & USART_ISR_TC) == 0U) && ((HAL_GetTick() - t0) < 50U))
  {
  }
  USART2->CR1 &= ~USART_CR1_UE;
  USART2->BRR = (HAL_RCC_GetPCLK1Freq() + (baud / 2U)) / baud;
  USART2->CR1 |= USART_CR1_UE;
}

/* Configure PA2/PA3 en USART2, l'USART2 et les deux canaux DMA */
static void uart_start(uint32_t baud)
{
  GPIO_InitTypeDef g = { 0 };

  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  g.Pin       = NCP_UART_TX_PIN | NCP_UART_RX_PIN;
  g.Mode      = GPIO_MODE_AF_PP;
  g.Pull      = GPIO_PULLUP;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(NCP_UART_PORT, &g);

  USART2->CR1 = 0U;
  USART2->CR2 = 0U;
  USART2->CR3 = USART_CR3_DMAR | USART_CR3_DMAT | USART_CR3_OVRDIS;
  USART2->BRR = (HAL_RCC_GetPCLK1Freq() + (baud / 2U)) / baud;
  USART2->ICR = 0xFFFFFFFFUL;

  /* DMA1 : canal 6 = USART2_RX, canal 7 = USART2_TX (requête 2) */
  DMA1_CSELR->CSELR = (DMA1_CSELR->CSELR & ~(DMA_CSELR_C6S | DMA_CSELR_C7S)) |
                      (2UL << DMA_CSELR_C6S_Pos) | (2UL << DMA_CSELR_C7S_Pos);
  DMA1_Channel6->CCR   = 0U;
  DMA1_Channel6->CPAR  = (uint32_t)&USART2->RDR;
  DMA1_Channel6->CMAR  = (uint32_t)s_rx_dma;
  DMA1_Channel6->CNDTR = RX_DMA_SIZE;
  DMA1_Channel6->CCR   = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PL_1 | DMA_CCR_EN;
  DMA1_Channel7->CCR   = 0U;
  DMA1_Channel7->CPAR  = (uint32_t)&USART2->TDR;
  DMA1->IFCR = DMA_IFCR_CGIF6 | DMA_IFCR_CGIF7;
  s_rx_rd = 0U;
  s_tx_head = 0U;
  s_tx_tail = 0U;
  s_tx_inflight = 0U;

  USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

/* Arrête l'USART2 et le DMA, remet PA2/PA3 en entrées (état de repos) */
static void uart_stop(void)
{
  GPIO_InitTypeDef g = { 0 };

  DMA1_Channel6->CCR = 0U;
  DMA1_Channel7->CCR = 0U;
  DMA1->IFCR = DMA_IFCR_CGIF6 | DMA_IFCR_CGIF7;
  USART2->CR1 = 0U;
  USART2->CR3 = 0U;
  __HAL_RCC_USART2_CLK_DISABLE();

  g.Pin  = NCP_UART_TX_PIN | NCP_UART_RX_PIN;    /* retour en entrées (repos) */
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(NCP_UART_PORT, &g);
}

/* Octets PC -> module : tampon circulaire vidé par le DMA */
static void tx_pump(void)
{
  uint32_t len;

  if ((s_tx_inflight != 0U) && ((DMA1->ISR & DMA_ISR_TCIF7) != 0U))
  {
    DMA1->IFCR = DMA_IFCR_CGIF7;
    s_tx_tail = (s_tx_tail + s_tx_inflight) & (TX_SIZE - 1U);
    s_tx_inflight = 0U;
  }
  if ((s_tx_inflight != 0U) || (s_tx_head == s_tx_tail))
  {
    return;
  }
  len = (s_tx_head - s_tx_tail) & (TX_SIZE - 1U);
  if (len > (TX_SIZE - s_tx_tail))
  {
    len = TX_SIZE - s_tx_tail;
  }
  DMA1_Channel7->CCR   = 0U;
  DMA1_Channel7->CMAR  = (uint32_t)&s_tx[s_tx_tail];
  DMA1_Channel7->CNDTR = len;
  DMA1->IFCR = DMA_IFCR_CGIF7;
  s_tx_inflight = len;
  DMA1_Channel7->CCR = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_PL_1 | DMA_CCR_EN;
}

/* Copie les octets reçus du PC (port COM virtuel) dans le tampon d'émission */
static uint32_t usb_to_uart(void)
{
  uint8_t  buf[64];
  uint32_t total = 0U;

  for (;;)
  {
    uint32_t room = (TX_SIZE - 1U) - ((s_tx_head - s_tx_tail) & (TX_SIZE - 1U));
    size_t   n;
    size_t   i;

    if (room == 0U)
    {
      break;
    }
    n = cdc_port_read(buf, (room < sizeof(buf)) ? room : sizeof(buf));
    if (n == 0U)
    {
      break;
    }
    for (i = 0U; i < n; i++)
    {
      s_tx[s_tx_head] = buf[i];
      s_tx_head = (s_tx_head + 1U) & (TX_SIZE - 1U);
    }
    total += (uint32_t)n;
  }
  tx_pump();
  return total;
}

/* Octets module -> PC : lus dans le tampon circulaire du DMA */
static uint32_t uart_to_usb(void)
{
  uint32_t wr = RX_DMA_SIZE - DMA1_Channel6->CNDTR;
  uint32_t total = 0U;

  if (wr >= RX_DMA_SIZE)
  {
    wr = 0U;
  }
  while (s_rx_rd != wr)
  {
    uint32_t end = (wr > s_rx_rd) ? wr : RX_DMA_SIZE;
    size_t   n = cdc_port_write(&s_rx_dma[s_rx_rd], end - s_rx_rd);

    if (n == 0U)
    {
      break;                            /* tampon USB plein : nouvel essai au tour suivant */
    }
    s_rx_rd = (s_rx_rd + (uint32_t)n) % RX_DMA_SIZE;
    total += (uint32_t)n;
  }
  return total;
}

/* Bouton User1 (ObCP) ou SW1 (X-NUCLEO) appuyé : fin de session demandée */
static bool button_pressed(void)
{
  return (HAL_GPIO_ReadPin(BTN_SW1_PORT, BTN_SW1_PIN) == GPIO_PIN_RESET) ||
         (HAL_GPIO_ReadPin(BTN_USER_PORT, BTN_USER_PIN) == GPIO_PIN_RESET);
}

/* LED RVB en magenta pendant la session (rouge + bleu) */
static void led_magenta(bool on, uint32_t saved[3])
{
  if (on)
  {
    saved[0] = __HAL_TIM_GET_COMPARE(&htim3, LED_R_CHANNEL);
    saved[1] = __HAL_TIM_GET_COMPARE(&htim3, LED_G_CHANNEL);
    saved[2] = __HAL_TIM_GET_COMPARE(&htim3, LED_B_CHANNEL);
    __HAL_TIM_SET_COMPARE(&htim3, LED_R_CHANNEL, LED_PWM_PERIOD / 4U);
    __HAL_TIM_SET_COMPARE(&htim3, LED_G_CHANNEL, 0U);
    __HAL_TIM_SET_COMPARE(&htim3, LED_B_CHANNEL, LED_PWM_PERIOD / 4U);
  }
  else
  {
    __HAL_TIM_SET_COMPARE(&htim3, LED_R_CHANNEL, saved[0]);
    __HAL_TIM_SET_COMPARE(&htim3, LED_G_CHANNEL, saved[1]);
    __HAL_TIM_SET_COMPARE(&htim3, LED_B_CHANNEL, saved[2]);
  }
}

/* -------------------------------------------------------------------------- */
/* Voir uart_bridge.h : boucle de relais, chaque tour :
 *   - suit le débit demandé par le PC ;
 *   - relaie PC -> module et module -> PC ;
 *   - demande à bridge_ctl.c si la session doit se terminer.            */
void uart_bridge_run(bridge_result_t *r)
{
  bridge_ctl_t ctl;
  bridge_end_t end = BRIDGE_END_NONE;
  uint32_t     t_start = HAL_GetTick();
  uint32_t     lc_count = cdc_port_line_coding_count();
  uint32_t     led_saved[3];

  memset(r, 0, sizeof(*r));
  r->baud = cdc_port_baudrate();
  uart_start(r->baud);
  cdc_port_set_always_send(true);
  led_magenta(true, led_saved);
  bridge_ctl_init(&ctl, cdc_port_dtr(), t_start);

  while (end == BRIDGE_END_NONE)
  {
    uint32_t in;
    uint32_t out;

    /* Le débit suit celui choisi par le programme du PC (500 kbit/s puis 2 Mbit/s) */
    if (cdc_port_line_coding_count() != lc_count)
    {
      lc_count = cdc_port_line_coding_count();
      r->baud = cdc_port_baudrate();
      uart_set_baud(r->baud);
    }
    in = usb_to_uart();
    out = uart_to_usb();
    end = bridge_ctl_step(&ctl, HAL_GetTick(), cdc_port_dtr(), in, out, button_pressed());
  }

  uart_stop();
  cdc_port_set_always_send(false);
  led_magenta(false, led_saved);
  cdc_port_tx_discard();                /* réponses binaires du module non lues */
  cdc_port_rx_flush();
  r->to_module = ctl.to_module;
  r->from_module = ctl.from_module;
  r->end = end;
  r->seconds = (HAL_GetTick() - t_start) / 1000U;
}

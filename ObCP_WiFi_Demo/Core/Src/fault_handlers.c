/**
 ******************************************************************************
 * @file    fault_handlers.c
 * @brief   Gestionnaires des fautes du Cortex-M4 (HardFault, MemManage,
 *          BusFault, UsageFault) et traitement de la NMI.
 *
 *          L'ObCP n'a pas de liaison série « directe » (la console passe par
 *          l'USB, qui ne fonctionne plus pendant une faute) : en cas de faute,
 *          l'adresse de l'instruction fautive et les registres d'état sont
 *          rangés dans les registres de sauvegarde de la RTC, la LED rouge
 *          clignote 2 secondes puis la carte redémarre ; le rapport est
 *          affiché au démarrage suivant.
 *
 *          Ces gestionnaires doivent lire la pile au moment de la faute : ils
 *          sont écrits en assembleur (fonctions « naked ») et ne peuvent donc
 *          pas se trouver dans les sections USER CODE de stm32l4xx_it.c. Leur
 *          génération est désactivée dans CubeMX (Pinout & Configuration >
 *          System Core > NVIC > Code generation : « Generate IRQ handler »
 *          décoché pour Hard fault, Memory management fault, Prefetch fault /
 *          memory access fault et Undefined instruction). La NMI reste
 *          générée : NMI_Handler() appelle fault_nmi() depuis sa section
 *          USER CODE.
 ******************************************************************************
 */
#include "main.h"
#include "board.h"
#include "config.h"

/* Prototypes (pas d'en-tête : fonctions appelées par la table des vecteurs
 * d'interruption du fichier de démarrage et par stm32l4xx_it.c)          */
void        fault_nmi(void);
void        hardfault_report(const uint32_t *frame, uint32_t kind);
void        HardFault_Handler(void);
void        MemManage_Handler(void);
void        BusFault_Handler(void);
void        UsageFault_Handler(void);

/* Clignotement de la LED rouge sans timer ni interruption (fautes) */
static void red_blink(uint32_t cycles)
{
  uint32_t r = (uint32_t)__builtin_ctz(LED_R_PIN);
  uint32_t g = (uint32_t)__builtin_ctz(LED_G_PIN);
  uint32_t b = (uint32_t)__builtin_ctz(LED_B_PIN);
  uint32_t i;
  uint32_t k;

  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
  (void)RCC->AHB2ENR;
  LED_G_PORT->BRR = LED_G_PIN;
  LED_B_PORT->BRR = LED_B_PIN;
  LED_R_PORT->MODER = (LED_R_PORT->MODER & ~(3UL << (r * 2U))) | (1UL << (r * 2U));   /* sorties */
  LED_G_PORT->MODER = (LED_G_PORT->MODER & ~(3UL << (g * 2U))) | (1UL << (g * 2U));
  LED_B_PORT->MODER = (LED_B_PORT->MODER & ~(3UL << (b * 2U))) | (1UL << (b * 2U));
  for (k = 0U; (cycles == 0U) || (k < cycles); k++)
  {
    LED_R_PORT->ODR ^= LED_R_PIN;
    for (i = 0U; i < (SystemCoreClock / 40U); i++)        /* ~ 100 ms */
    {
      __NOP();
    }
  }
}

/* NMI : une double erreur ECC en lisant la page de configuration en flash
 * déclenche une NMI ; elle est acquittée et signalée à config.c (la page est
 * alors considérée comme abîmée). Toute autre NMI est traitée comme une faute. */
void fault_nmi(void)
{
  board_fault_t f = { 4U, 0U, 0U, 0U, 0U, 0U };

  if (config_ecc_nmi())
  {
    return;
  }
  board_fault_save(&f);
  red_blink(20U);
  NVIC_SystemReset();
}

/* Récupère le pointeur de pile actif (MSP ou PSP) puis appelle le rapport
 * (assembleur « basique » uniquement : fonctions naked).
 * Lors d'une faute, le processeur empile automatiquement 8 registres
 * (R0-R3, R12, LR, PC, xPSR) ; le bit 2 de LR indique quelle pile était
 * utilisée. hardfault_report() reçoit l'adresse de ces registres.        */
#define FAULT_STR2(x)  #x
#define FAULT_STR(x)   FAULT_STR2(x)
#define FAULT_TRAMPOLINE(kind)                \
  __asm volatile(                             \
    " tst   lr, #4            \n"             \
    " ite   eq                \n"             \
    " mrseq r0, msp           \n"             \
    " mrsne r0, psp           \n"             \
    " mov   r1, #" FAULT_STR(kind) " \n"      \
    " b     hardfault_report  \n")

/* Les quatre gestionnaires ne diffèrent que par le numéro de la faute.
 * « naked » : le compilateur n'ajoute ni prologue ni épilogue, la pile
 * reste exactement celle de la faute.                                   */
__attribute__((naked)) void HardFault_Handler(void)
{
  FAULT_TRAMPOLINE(0);
}

__attribute__((naked)) void MemManage_Handler(void)
{
  FAULT_TRAMPOLINE(1);
}

__attribute__((naked)) void BusFault_Handler(void)
{
  FAULT_TRAMPOLINE(2);
}

__attribute__((naked)) void UsageFault_Handler(void)
{
  FAULT_TRAMPOLINE(3);
}

/* frame[5] = LR et frame[6] = PC empilés au moment de la faute ; les registres
 * CFSR, HFSR et BFAR du Cortex-M4 en donnent la cause (voir PM0214, ST).
 * « used » : appelée seulement depuis l'assembleur, elle ne doit pas être
 * supprimée par l'optimisation.                                          */
__attribute__((used)) void hardfault_report(const uint32_t *frame, uint32_t kind)
{
  board_fault_t f;

  __disable_irq();
  f.kind = kind & 3U;
  f.pc   = frame[6];
  f.lr   = frame[5];
  f.cfsr = SCB->CFSR;
  f.hfsr = SCB->HFSR;
  f.bfar = SCB->BFAR;
  board_fault_save(&f);
  red_blink(20U);                      /* ~ 2 s */
  NVIC_SystemReset();
}

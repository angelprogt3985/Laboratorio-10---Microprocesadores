
#include "stm32g431xx.h"
#include <stdint.h>

volatile uint32_t periodo_us = 0;
volatile uint32_t ancho_us = 0;

char mensaje[64];
volatile uint32_t tx_indice = 0;
volatile uint32_t tx_longitud = 0;
volatile uint32_t tx_ocupado = 0;

uint32_t Frecuencia_Timer(uint32_t apb2)
{
    SystemCoreClockUpdate();

    uint32_t posicion = apb2 ?
        RCC_CFGR_PPRE2_Pos : RCC_CFGR_PPRE1_Pos;

    uint32_t ppre = (RCC->CFGR >> posicion) & 7U;

    if (ppre < 4U)
        return SystemCoreClock;

    uint32_t divisor = 1U << (ppre - 3U);

    return (SystemCoreClock / divisor) * 2U;
}

void PWM_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;

    GPIOA->MODER &= ~(3U << 16);
    GPIOA->MODER |=  (2U << 16);

    GPIOA->AFR[1] &= ~(15U << 0);
    GPIOA->AFR[1] |=  (6U << 0);

    uint32_t ftim = Frecuencia_Timer(1);
    TIM1->PSC = (ftim / 1000000U) - 1U;

    TIM1->ARR = 199;
    TIM1->CCR1 = 100;

    TIM1->CCMR1 = (6U << 4) | TIM_CCMR1_OC1PE;

    TIM1->CCER = TIM_CCER_CC1E;
    TIM1->BDTR |= TIM_BDTR_MOE;

    TIM1->CR1 |= TIM_CR1_ARPE;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CR1 |= TIM_CR1_CEN;
}

void InputCapture_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;

    GPIOA->MODER &= ~(3U << 0);
    GPIOA->MODER |=  (2U << 0);

    GPIOA->AFR[0] &= ~(15U << 0);
    GPIOA->AFR[0] |=  (1U << 0);

    uint32_t ftim = Frecuencia_Timer(0);
    TIM2->PSC = (ftim / 1000000U) - 1U;

    TIM2->ARR = 0xFFFFFFFFU;

    TIM2->CCMR1 = (1U << 0) | (2U << 8);

    TIM2->CCER = TIM_CCER_CC1E |
                 TIM_CCER_CC2E |
                 TIM_CCER_CC2P;

    TIM2->SMCR = (5U << 4) | (4U << 0);

    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;

    TIM2->DIER = TIM_DIER_CC1IE |
                 TIM_DIER_CC2IE;

    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM2_IRQHandler(void)
{
    uint32_t estado = TIM2->SR;

    if (estado & TIM_SR_CC1IF)
    {
        periodo_us = TIM2->CCR1;
    }

    if (estado & TIM_SR_CC2IF)
    {
        ancho_us = TIM2->CCR2;
    }

    TIM2->SR = ~(TIM_SR_CC1IF | TIM_SR_CC2IF);
}

void Agregar_Texto(const char *texto, uint32_t *n)
{
    while (*texto != '\0' && *n < 63U)
    {
        mensaje[*n] = *texto;
        (*n)++;
        texto++;
    }
}

void Agregar_Numero(uint32_t numero, uint32_t *n)
{
    char digitos[10];
    uint32_t cantidad = 0;

    do
    {
        digitos[cantidad++] = '0' + (numero % 10U);
        numero /= 10U;
    }
    while (numero != 0U);

    while (cantidad > 0U && *n < 63U)
    {
        mensaje[(*n)++] = digitos[--cantidad];
    }
}

void Preparar_Respuesta(char comando)
{
    uint32_t n = 0;
    uint32_t p = periodo_us;
    uint32_t a = ancho_us;

    if (comando < '0' || comando > '3')
        return;

    if (p == 0U || a == 0U || a > p || p > 1000000U)
    {
        Agregar_Texto("Sin medicion valida", &n);
    }
    else
    {
        switch (comando)
        {
            case '0':
                Agregar_Texto("Periodo = ", &n);
                Agregar_Numero(p, &n);
                Agregar_Texto(" us", &n);
                break;

            case '1':
                Agregar_Texto("Frecuencia = ", &n);
                Agregar_Numero((1000000U + p / 2U) / p, &n);
                Agregar_Texto(" Hz", &n);
                break;

            case '2':
                Agregar_Texto("Ancho de pulso = ", &n);
                Agregar_Numero(a, &n);
                Agregar_Texto(" us", &n);
                break;

            case '3':
                Agregar_Texto("Duty Cycle = ", &n);
                Agregar_Numero((a * 100U + p / 2U) / p, &n);
                Agregar_Texto("%", &n);
                break;
        }
    }

    Agregar_Texto("\r\n", &n);

    tx_indice = 0;
    tx_longitud = n;
    tx_ocupado = 1;

    USART1->CR1 |= USART_CR1_TXEIE_TXFNFIE;
}

void UART_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    GPIOA->MODER &= ~((3U << 18) | (3U << 20));
    GPIOA->MODER |=  ((2U << 18) | (2U << 20));

    GPIOA->AFR[1] &= ~((15U << 4) | (15U << 8));
    GPIOA->AFR[1] |=  ((7U << 4) | (7U << 8));

    GPIOA->PUPDR &= ~(3U << 20);
    GPIOA->PUPDR |=  (1U << 20);

    RCC->CCIPR &= ~RCC_CCIPR_USART1SEL;

    uint32_t ftim = Frecuencia_Timer(1);

    uint32_t ppre =
        (RCC->CFGR >> RCC_CFGR_PPRE2_Pos) & 7U;

    uint32_t divisor =
        (ppre < 4U) ? 1U : (1U << (ppre - 3U));

    uint32_t f_uart = SystemCoreClock / divisor;

    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = 0;

    USART1->BRR = (f_uart + 57600U) / 115200U;

    USART1->CR1 = USART_CR1_TE |
                  USART_CR1_RE |
                  USART_CR1_RXNEIE_RXFNEIE |
                  USART_CR1_UE;

    NVIC_SetPriority(USART1_IRQn, 2);
    NVIC_EnableIRQ(USART1_IRQn);

    (void)ftim;
}

void USART1_IRQHandler(void)
{
    uint32_t estado = USART1->ISR;

    if (estado & (USART_ISR_ORE |
                  USART_ISR_FE |
                  USART_ISR_NE))
    {
        USART1->ICR = USART_ICR_ORECF |
                      USART_ICR_FECF |
                      USART_ICR_NECF;
    }

    if (estado & USART_ISR_RXNE_RXFNE)
    {
        char comando = (char)(USART1->RDR & 0xFFU);

        if (tx_ocupado == 0U)
        {
            Preparar_Respuesta(comando);
        }
    }

    if ((USART1->ISR & USART_ISR_TXE_TXFNF) &&
        (USART1->CR1 & USART_CR1_TXEIE_TXFNFIE))
    {
        if (tx_indice < tx_longitud)
        {
            USART1->TDR = mensaje[tx_indice++];
        }
        else
        {
            USART1->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
            tx_ocupado = 0;
        }
    }
}

int main(void)
{
    SystemCoreClockUpdate();

    InputCapture_Init();
    UART_Init();
    PWM_Init();

    while (1)
    {
        __WFI();
    }
}

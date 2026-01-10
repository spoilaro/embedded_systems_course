#include <stdio.h> // we can now use this
#include <sleep.h>
#include <stdlib.h>
#include <bitwise.h>
#include <stm32f4xx.h>

extern uint32_t SystemCoreClock; // system clock frequency

// convert baud into BRR value
#define baud(bps) \
	(((SystemCoreClock/((bps)*16)) << 4) | ((SystemCoreClock/(bps)) % 16))

// All the possible states for the system
#define CONFIG 0
#define IDLE 1
#define MODULATE 2
#define REDUCE 3
#define TEST 4

// Setup structs for input buttons
typedef struct ButtonState {
    uint8_t button_prev;
    uint8_t button_now;
    uint8_t pin;
    uint8_t status;
} ButtonState;

// Semaphore
volatile int timer_lock = 0;

void status_print(uint8_t status) {

    printf("STATUS: %d\r\n");

    switch (status) {
        case CONFIG:
            printf("State: CONFIG\r\n");
            break;
        case IDLE:
            printf("State: IDLE\r\n");
            break;
        case MODULATE:
            printf("State: MODULATE\r\n");
            break;
        case REDUCE:
            printf("State: REDUCE\r\n");
            break;
        case TEST:
            printf("State: TEST\r\n");
            break;
    }
}

// Function to release semaphore
void lock_release() {
    timer_lock = 0;
    return;
}

// TODO: semaphore with 5 second timer
// timer 2 IRQ handler (not important, toggles LED)
void  __attribute__((interrupt("IRQ"))) TIM2_IRQHandler()
{
    // Release semaphore lock
	lock_release();
    printf("Lock released\r\n");

    // clear interrupt flag
	TIM2->SR &= ~TIM_SR_UIF;
    
}

void lock_acquire() {
    int check = 1;
    while(check) {
        while(timer_lock == 1);
        // Disable interrupts
        __disable_irq();
        if(timer_lock == 0) {
           timer_lock = 1;

            printf("Lock acquired!!!!\r\n");
            // Start timer
            TIM2->CNT = 0;
            TIM2->CR1 |= TIM_CR1_CEN;

           check = 0;
        }
        // Enable interrupts again
        __enable_irq();
    }
    return;
}


void setup_timer() {
    // Enable TIM2
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // Prescaler: 16 MHz / 1600 = 10 kHz
    TIM2->PSC = 1600 - 1;

    // Auto-reload: 10 kHz * 5 s = 50,000
    TIM2->ARR = 50000 - 1;

    // One-pulse mode
    TIM2->CR1 |= TIM_CR1_OPM | TIM_CR1_URS;

    // Enable update interrupt, clear interrupt flag, and reset counter
    TIM2->DIER |= TIM_DIER_UIE;
	TIM2->SR &= ~TIM_SR_UIF;
    TIM2->CNT = 0;
    
    // Enable TIM2 interrupt in NVIC
    NVIC_EnableIRQ(TIM2_IRQn);
}

ButtonState *setup_buttons(ButtonState *states) {
    // TODO: initialize 4 buttons
    
    ButtonState configButton = {
        1, 1, 8, 0
    };

    ButtonState idleButton = {
        1, 1, 9, 1
    };
    
    ButtonState modulateButton = {
        1, 1, 6, 2
    };
    
    ButtonState reduceButton = {
        1, 1, 5, 3
    };

    ButtonState userButton = {
        1, 1, 13, 4
    };



    states[0] = userButton;

    // temp button PIN13 as input, set as pull-up
    GPIOC->MODER |= (0<<27); 
    GPIOC->MODER |= (0<<26);
    GPIOC->PUPDR |= (1<<26);
    GPIOC->PUPDR |= (0<<27);

    // Config button PIN8 as input, set as pull-up
    GPIOC->MODER |= (0<<17); 
    GPIOC->MODER |= (0<<16);
    GPIOC->PUPDR |= (0<<17); 
    GPIOC->PUPDR |= (0<<16);
    // Idle button PIN9 as input, set as pull-up
    GPIOC->MODER |= (0<<18); 
    GPIOC->MODER |= (0<<19);
    GPIOC->PUPDR |= (0<<18); 
    GPIOC->PUPDR |= (0<<19);
    // Modulate button PIN6 as input, set as pull-up
    GPIOC->MODER |= (0<<13); 
    GPIOC->MODER |= (0<<12);
    GPIOC->PUPDR |= (0<<13); 
    GPIOC->PUPDR |= (0<<12);
    // Reduce button PIN5 as input, set as pull-up
    GPIOC->MODER |= (0<<11); 
    GPIOC->MODER |= (0<<10);
    GPIOC->PUPDR |= (0<<11); 
    GPIOC->PUPDR |= (0<<10);

    return states;
}


int main()
{
	// enable GPIOC clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
	// enable GPIOA clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	// enable USART2 clock
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    setup_timer();

    ButtonState initState[1];
    ButtonState *states;

    states = setup_buttons(initState);

	// set PA2 & PA3 alternate functions to AF7 (USART2 RX/TX)
	bits_val(GPIOA->AFR[0], 4, 2, 7); // PA2 -> USART2_TX
	bits_val(GPIOA->AFR[0], 4, 3, 7); // PA3 -> USART2_RX
	bits_val(GPIOA->MODER , 2, 2, 2); // PA2 -> alternate function mode
	bits_val(GPIOA->MODER , 2, 3, 2); // PA3 -> alternate function mode

	// configure UART as 8N1 at 115200bps
	USART2->BRR  = baud(115200);
	USART2->CR1 |= USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;

	while (1)
	{
		// wait for button state to change

        for (int i=0; i<1; i++) {
            states[i].button_now = GPIOC->IDR & (1 << states[i].pin) ? 1 : 0;

            if (states[i].button_prev == 1  && states[i].button_now == 0) {            
               lock_acquire();
               status_print((uint8_t)states[i].status);
                
            }

            states[0].button_prev = states[0].button_now;
        }
	}

}

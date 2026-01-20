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

// Button definitions for the system
#define MODE 0
#define TOGGLE 1
#define INCREASE 2
#define REDUCE 3

// Setup structs for input buttons
typedef struct Button {
    uint8_t button_prev;
    uint8_t button_now;
    uint8_t pin;
    uint8_t name;
} Button;

// Semaphore
volatile int timer_lock = 0;
volatile int localState = IDLE;

void state_print(int localState) {

    switch (localState) {
        case CONFIG:
            printf("State: CONFIG\r\n");
            break;
        case IDLE:
            printf("State: IDLE\r\n");
            break;
        case INCREASE:
            printf("State: MODULATE\r\n");
            break;
    }
}

void change_state() {

                lock_acquire();
                switch (localState)
                {
                case CONFIG:
                    localState = IDLE;
                    break;
                case IDLE:
                    localState = MODULATE;
                    break;
                case MODULATE:
                    localState = CONFIG;
                    break;
                }

                state_print(localState);
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

Button *setup_buttons(Button *buttons) {
    // Buttons 5, 6 & 8 are additional hardware buttons soldered to the board.
    // Button 13 is the built-in button of the board.
    
    Button toggleButton = {
        1, 1, 8, TOGGLE
    };

    Button increaseButton = {
        1, 1, 6, INCREASE
    };
    
    Button reduceButton = {
        1, 1, 5, REDUCE
    };

    Button modeButton = {
        1, 1, 13, MODE
    };



    // states[0] = modeButton;
    // states[1] = toggleButton;
    // states[2] = increaseButton;
    // states[3] = reduceButton;


    buttons[0] = modeButton;

    // temp button PIN13 as input, set as pull-up
    GPIOC->MODER |= (0<<26);
    GPIOC->MODER |= (0<<27); 
    GPIOC->PUPDR |= (1<<26);
    GPIOC->PUPDR |= (0<<27);

    // Config button PIN8 as input, set as pull-up
    GPIOC->MODER |= (0<<16);
    GPIOC->MODER |= (0<<17); 
    GPIOC->PUPDR |= (1<<16);
    GPIOC->PUPDR |= (0<<17); 

    // Modulate button PIN6 as input, set as pull-up
    GPIOC->MODER |= (0<<12);
    GPIOC->MODER |= (0<<13); 
    GPIOC->PUPDR |= (1<<12);
    GPIOC->PUPDR |= (0<<13); 
    
    // Reduce button PIN5 as input, set as pull-up
    GPIOC->MODER |= (0<<10);
    GPIOC->MODER |= (0<<11); 
    GPIOC->PUPDR |= (1<<10);
    GPIOC->PUPDR |= (0<<11); 

    return buttons;
}

void print_bits(uint32_t value)
{
    for (int i = 15; i >= 0; i--)
    {
        printf("%c", (value & (1U << i)) ? '1' : '0');
    }
    printf("\r\n");
}



int main()
{
	// enable GPIOC clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
	// enable GPIOA clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	// enable USART2 clock
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    GPIOC->AFR[1] &= ~(0xF << ((8 - 8) * 4)); // clear AF for PC8

    setup_timer();

    Button initButtons[1];
    Button *buttons;

    int localState = 1;

    buttons = setup_buttons(initButtons);

	// set PA2 & PA3 alternate functions to AF7 (USART2 RX/TX)
	bits_val(GPIOA->AFR[0], 4, 2, 7); // PA2 -> USART2_TX
	bits_val(GPIOA->AFR[0], 4, 3, 7); // PA3 -> USART2_RX
	bits_val(GPIOA->MODER , 2, 2, 2); // PA2 -> alternate function mode
	bits_val(GPIOA->MODER , 2, 3, 2); // PA3 -> alternate function mode

	// configure UART as 8N1 at 115200bps
	USART2->BRR  = baud(115200);
	USART2->CR1 |= USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;


    // Setup calculation variables
    float kp;
    float ki;

	while (1)
	{

        for (int i = 0; i < 1; i++)
        {
            buttons[i].button_now = GPIOC->IDR & (1 << buttons[i].pin) ? 1 : 0;

            if (buttons[i].button_prev == 1 && buttons[i].button_now == 0)
            {
                switch(buttons[i].name) {
                    case CONFIG:
                        change_state();
                        break;
                    case IDLE:
                        printf("HELLO FROM IDLE \r\n");
                        break;
                    case INCREASE:
                        printf("HELLO FROM INCREASE \r\n");
                        break;
                    case REDUCE:
                        printf("HELLO FROM REDUCE \r\n");
                        break;
                }
            }

            buttons[i].button_prev = buttons[i].button_now;
        }
    }
}

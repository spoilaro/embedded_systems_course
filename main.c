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

// Setup calculation variables
float kp;
float ki;
// 0 for kp, 1 for ki
int currentVariable = 0; 


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

                long_lock_acquire();
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

// 
void  __attribute__((interrupt("IRQ"))) TIM2_IRQHandler()
{
    // Release semaphore lock
	lock_release();
    printf("Long lock released\r\n");

    // clear interrupt flag
	TIM2->SR &= ~TIM_SR_UIF;
    
}

// 
void  __attribute__((interrupt("IRQ"))) TIM3_IRQHandler()
{
    // Release semaphore lock
	lock_release();
    printf("Short lock released\r\n");

    // clear interrupt flag
	TIM3->SR &= ~TIM_SR_UIF;
}

void long_lock_acquire() {
    int check = 1;
    while(check) {
        while(timer_lock == 1);
        // Disable interrupts
        __disable_irq();
        if(timer_lock == 0) {
           timer_lock = 1;

            printf("Long lock acquired!!!!\r\n");
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

void short_lock_acquire() {
    int check = 1;
    while(check) {
        while(timer_lock == 1);
        // Disable interrupts
        __disable_irq();
        if(timer_lock == 0) {
           timer_lock = 1;

            printf("Short lock acquired!!!!\r\n");
            // Start timer
            TIM3->CNT = 0;
            TIM3->CR1 |= TIM_CR1_CEN;

           check = 0;
        }
        // Enable interrupts again
        __enable_irq();
    }
    return;
}


void setup_timer() {
    // Enable TIM2 and TIM3
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM3EN;

    // Prescaler: 16 MHz / 1600 = 10 kHz
    TIM2->PSC = 1600 - 1;
    TIM3->PSC = 1600 - 1;

    // Auto-reload: TIM2 -> 10 kHz * 5 s = 50,000, TIM3 -> 10kHz * 2,5 = 25000
    TIM2->ARR = 50000 - 1;
    TIM3->ARR = 25000 - 1;

    // One-pulse mode
    TIM2->CR1 |= TIM_CR1_OPM | TIM_CR1_URS;

    // Enable update interrupt, clear interrupt flag, and reset counter
    TIM2->DIER |= TIM_DIER_UIE;
	TIM2->SR &= ~TIM_SR_UIF;
    TIM2->CNT = 0;

    TIM3->DIER |= TIM_DIER_UIE; 
	TIM3->CR1  |= TIM_CR1_OPM | TIM_CR1_URS;
    TIM3->CNT = 0;
	
    
    // Enable TIM2 interrupt in NVIC
    NVIC_EnableIRQ(TIM2_IRQn);
    // Enable TIM3 interrupt in NVIC
    NVIC_EnableIRQ(TIM3_IRQn);  

}

void setup_leds() {

    // !!! Used LED pins PC8, PB5 & PB10 !!!

    // Set pin PC8 mode to be output
    GPIOC->MODER |= (1<<16);
    GPIOC->MODER |= (0<<17); 

    // Set pin PB5 mode to be output
    GPIOB->MODER |= (1<<10);
    GPIOB->MODER |= (0<<11); 

    // Set pin PB10 mode to be output
    GPIOB->MODER |= (1<<20);
    GPIOB->MODER |= (0<<21); 

}

Button *setup_buttons(Button *buttons) {
    // Buttons 5, 6 & 8 are additional hardware buttons soldered to the board.
    // Button 13 is the built-in button of the board.
    
    Button toggleButton = {
        1, 1, 9, TOGGLE
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
    buttons[1] = toggleButton;
    buttons[2] = increaseButton;
    buttons[3] = reduceButton;

    // temp button PIN13 as input, set as pull-up
    GPIOC->MODER |= (0<<26);
    GPIOC->MODER |= (0<<27); 
    GPIOC->PUPDR |= (1<<26);
    GPIOC->PUPDR |= (0<<27);

    // Config button PIN9 as input, set as pull-up
    GPIOC->MODER |= (0<<18);
    GPIOC->MODER |= (0<<19); 
    GPIOC->PUPDR |= (1<<18);
    GPIOC->PUPDR |= (0<<19); 

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

void handleToggleButton() {
    switch (localState) {
        case CONFIG:
            currentVariable = 0 ? 1 : 0;
            break;
        break;
    }
}


// TODO: change reference voltage in MODULATE state

void handleIncreaseButton() {
    switch (localState) {
        case CONFIG:
            // currentVariable = 0 ? kp += 0.1 : ki += 0.1;
            break;

        case MODULATE:
            // TODO: Implement modulate handling
            break;
        
    }
}

void handleReduceButton() {
    switch (localState) {
        case CONFIG:
            // currentVariable = 0 ? kp -= 0.1 : ki -= 0.1;
            break;

        case MODULATE:
            // TODO: Implement modulate handling
            break;
        
    }
}


void pi_controller() {
    
}

void converter_model() {
    static float i_1 = 0;
    static float i_2 = 0;
    static float i_3 = 0; 
    static float u_1 = 0;
    static float u_2 = 0;
    static float u_3 = 0;

    float u_in = 1.5;

    i_1 = 0.9652*i_1 - 0.0172*u_1 + 0.0057*i_2 - 0.0058*u_2 + 0.0052*i_3 - 0.0251*u_3 + 0.0471*u_in;
    u_1 = 0.7732*i_1 + 0.1252*u_1 + 0.2315*i_2 + 0.0700*u_2 + 0.1282*i_3 + 0.7754*u_3 + 0.0377*u_in;
    i_2 = 0.8278*i_1 - 0.7522*u_1 - 0.0956*i_2 + 0.3299*u_2 - 0.4855*i_3 + 0.3915*u_3 + 0.0404*u_in;
    u_2 = 0.9948*i_1 + 0.2655*u_1 - 0.3848*i_2 + 0.4212*u_2 + 0.3927*i_3 + 0.2899*u_3 + 0.0485*u_in;
    i_3 = 0.7648*i_1 - 0.4165*u_1 - 0.4855*i_2 - 0.3366*u_2 - 0.0986*i_3 + 0.7281*u_3 + 0.0373*u_in:
    u_3 = 1.1056*i_1 + 0.7587*u_1 + 0.1179*i_2 + 0.0748*u_2 - 0.2192*i_3 + 0.1491*u_3 + 0.0539*u_in;

}

int main()
{
	// enable GPIOC clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
	// enable GPIOA clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    // Enable GPIOB clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
	// enable USART2 clock
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;


    ////////////////////////////////////////

    // Enabled timer for TIM4 which is used as PWM output
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    GPIOB->MODER &= ~(3 << 6 * 2);
    GPIOB->MODER |= (2 << 6 * 2);
    TIM4->PSC = 16 - 1;
    TIM4->ARR = 1000 - 1;

    TIM4->CCR1 = 0;
    TIM4->CCMR1 &= ~(7 << 4);
    TIM4->CCMR1 |=  (6 << 4);
    TIM4->CCMR1 |=  (1 << 3);  // preload enable

    TIM4->CCER |= (1 << 0);    // enable CH1 output
    TIM4->CR1  |= (1 << 7);    // ARPE
    TIM4->CR1  |= (1 << 0);    // start timer

    {
        for (int d = 0; d <= 1000; d++)
        {
            TIM4->CCR1 = d;
            for (volatile int i = 0; i < 2000; i++);
        }

        for (int d = 1000; d >= 0; d--)
        {
            TIM4->CCR1 = d;
            for (volatile int i = 0; i < 2000; i++);
        }
    }
    ////////////////////////////////////////

    GPIOC->AFR[1] &= ~(0xF << ((8 - 8) * 4)); // clear AF for PC8

    setup_timer();
    setup_leds();

    Button initButtons[4];
    Button *buttons;

    buttons = setup_buttons(initButtons);

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

        for (int i = 0; i < 4; i++)
        {
            buttons[i].button_now = GPIOC->IDR & (1 << buttons[i].pin) ? 1 : 0;

            if (buttons[i].button_prev == 1 && buttons[i].button_now == 0)
            {
                switch(buttons[i].name) {
                    case CONFIG:
                        change_state();
                        break;
                    case TOGGLE:
                        printf("HELLO FROM TOGGLE \r\n");
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

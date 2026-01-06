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
#define IDLE = 0
#define CONFIG = 1
#define MODULATE = 2



volatile int timer_lock = 0;

// TODO: semaphore with 5 second timer
void lock_init() {

}

void lock_release() {
    __disable_irq();
    timer_lock = 0;
    __enable_irq();
    return;
}

void lock_acquire() {
    int check = 1;
    while(check) {
        while(timer_lock == 1);
        // Disable interrupts
        __disable_irq();
        if(timer_lock == 0) {
           timer_lock = 1;
           check = 0;
        }
        // Enable interrupts again
        __enable_irq();
    }
    return;
}


int main()
{

	int now = 0, old = 1; // variables

	// enable GPIOC clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

	// configure user button (PC13) as pull-up input
	bits_val(GPIOC->PUPDR, 2, 13, 1);

	// enable GPIOA clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

	// enable USART2 clock
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

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
		if ((now = bit_get(GPIOC->IDR, 13)) != old) {
            old = now;
            if(!timer_lock) {
                printf("Trying to acquire lock\r\n");
                lock_acquire();
            } else {
                printf("Trying to release lock\r\n");
                lock_release();
            }
		}
        printf("Lock status: %d\r\n", timer_lock);
        sleep(1);
	}
}

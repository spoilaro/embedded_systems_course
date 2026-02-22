#include <stdio.h> // we can now use this
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

// 'K_DELTA' describes how much 'kp' and 'ki' are increased or decreased with one step.
#define K_DELTA 0.5

// The program stores buttons as 'Button' structs. 'Button' includes required
// fields for checking if the button is pressed and also the pin mask in order
// to access it.
typedef struct Button {
    uint8_t button_prev;
    uint8_t button_now;
    uint8_t pin;
    uint8_t name;
} Button;

// Semaphore
volatile int timer_lock = 0;
volatile int localState = MODULATE;

float reference_voltage = 1.5;
float static output_voltage = 1.5;



#define KP_ACTIVE 0
#define KI_ACTIVE 1

int currentVariable = KP_ACTIVE; 

// PI controller values
typedef struct {
    float Kp;           // Proportional part
    float Ki;           // Integral part
    float Ts;           // Measurement interval
    float integral;     
    float u_min;        // Lower bound for output voltage
    float u_max;        // Upper bound for output voltage
} PI_Controller;

// PI controller struct, which includes all the necessary values for running the PI algorithm
PI_Controller pi_controller;

// Declaring functions
float converter_model(float u_in);
float PI_Update(PI_Controller *pi, float measurement);
void lock_acquire();

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

void change_state()
{

    lock_acquire();
    switch (localState)
    {
    case CONFIG:
        localState = IDLE;
        GPIOB->ODR |= (1 << 10);
        GPIOC->ODR &= ~(1 << 8);
        break;

    case IDLE:
        localState = MODULATE;
        GPIOB->ODR &= ~(1 << 10);
        TIM4->CR1 |= TIM_CR1_CEN;
        break;

    case MODULATE:
        localState = CONFIG;
        GPIOC->ODR |= (1 << 8);
        TIM4->CR1 &= ~(TIM_CR1_CEN);
        TIM4->CCR1 = 0;
        break;
    }

    state_print(localState);
}

// Function to release semaphore
void lock_release() {
    timer_lock = 0;
    return;
}

// Interrupt routine for timer semaphore
void  __attribute__((interrupt("IRQ"))) TIM2_IRQHandler()
{
    // Release semaphore lock
	lock_release();
    printf("Lock released\r\n");

    // clear interrupt flag
	TIM2->SR &= ~TIM_SR_UIF;
    
}

// Interrupt routine for PI controller
void  __attribute__((interrupt("IRQ"))) TIM3_IRQHandler()
{
    // Run converter model
    output_voltage = converter_model(output_voltage);
    output_voltage = PI_Update(&pi_controller, output_voltage);

    // clear interrupt flag
	TIM3->SR &= ~TIM_SR_UIF;

    TIM3->CNT = 0;
    TIM3->CR1 |= TIM_CR1_CEN;
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
    // Enable TIM2 and TIM3
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM3EN;

    // Prescaler: 16 MHz / 1600 = 10 kHz
    TIM2->PSC = 1600 - 1;
    TIM3->PSC = 1600 - 1;

    // Auto-reload: TIM2 -> 10 kHz * 5 s = 50,000, TIM3 -> 10kHz * 0.5 = 5000
    TIM2->ARR = 50000 - 1;
    TIM3->ARR = 5000 - 1;

    // One-pulse mode
    TIM2->CR1 |= TIM_CR1_OPM | TIM_CR1_URS;

    // Enable update interrupt, clear interrupt flag, and reset counter
    TIM2->DIER |= TIM_DIER_UIE;
	TIM2->SR &= ~TIM_SR_UIF;
    TIM2->CNT = 0;

    TIM3->DIER |= TIM_DIER_UIE; 
	TIM3->CR1  |= TIM_CR1_URS;
    TIM3->CNT = 0;
	
    
    // Enable TIM2 interrupt in NVIC
    NVIC_EnableIRQ(TIM2_IRQn);
    // Enable TIM3 interrupt in NVIC
    NVIC_EnableIRQ(TIM3_IRQn);  

}

void setup_leds() {

    // PB6 LED = MODULATE LED, RED COLOR
    GPIOB->MODER |= (0 << 12);
    GPIOB->MODER |= (1 << 13);

    // PB10 LED = IDLE LED, BLUE LED
    GPIOB->MODER |= (1 << 20);
    GPIOB->MODER |= (0 << 21);
    
    // PC8 LED = CONFIG LED, GREEN LED
    GPIOC->MODER |= (1 << 16);
    GPIOC->MODER |= (0 << 17);

    // TODO: Set this back
    // Set the alternate function to AF2 for TIM4_CH1
    GPIOB->AFR[0] &= ~(0xF << (6 * 4)); // Clear alternate function
    GPIOB->AFR[0] |= (2 << (6 * 4));    // Set to AF2

    TIM4->PSC = 1600 - 1;
    TIM4->ARR = 1000 - 1;

    TIM4->CCR1 = 0; // Duty cycle

    TIM4->CCMR1 &= ~TIM_CCMR1_OC1M; // Clear PWM mode bits
    TIM4->CCMR1 |= (6 << TIM_CCMR1_OC1M_Pos); // Set to PWM mode 1

    TIM4->CCMR1 |= TIM_CCMR1_OC1PE;    // Preload enable

    TIM4->CCER |= TIM_CCER_CC1E;    // enable CH1 output

    TIM4->CR1 |= TIM_CR1_CEN;
    

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


void handleToggleButton() {
    switch (localState) {
        case CONFIG:

            if (currentVariable == KP_ACTIVE) {
                currentVariable = KI_ACTIVE;
            } else {
                currentVariable = KP_ACTIVE;
            }
        break;
    }
}



void handleIncreaseButton(PI_Controller *pi_controller) {
    switch (localState) {
        case CONFIG:
            if(currentVariable == KP_ACTIVE) {
                pi_controller->Kp += K_DELTA;
            } else {
                pi_controller->Ki += K_DELTA;
            }
            break;

    }
}

void handleReduceButton(PI_Controller *pi_controller) {
    switch (localState) {
        case CONFIG:
            if(currentVariable == KP_ACTIVE) {
                pi_controller->Kp -= K_DELTA;
            } else {
                pi_controller->Ki -= K_DELTA;
            }
            break;
    }
}


void PI_Init(PI_Controller *pi, float Kp, float Ki, float Ts, float u_min, float u_max) {
    /// Function for initializing the PI_Controller structure
    ///
    ///  PI_Controller *pi      Pointer to which the PI_Controller struct is assigned
    ///  float Kp               Proportional part
    ///  float Ki               Integral part
    ///  float Ts               Measurement interval - 2 Hz
    ///  float u_min            Minimum possible value for the voltage
    ///  float u_max            Maximum possible value for the voltage


    pi->Kp = Kp;
    pi->Ki = Ki;
    pi->Ts = Ts;
    pi->integral = 0.0f;
    pi->u_min = u_min;
    pi->u_max = u_max;
}

float PI_Update(PI_Controller *pi, float measurement) {
    /// Function for running the updating algorithm for PI_Controller. The
    /// function returns the new calculated voltage which is given to the
    /// converter model.
    ///   
    /// PI_Controller *pi       Pointer to which the PI_Controller struct is accessible from
    /// float setpoint          Desired reference voltage
    /// float measurement       Actual measured voltage got from converter model
    
    float error = reference_voltage - measurement;

    // Proportional part
    float P = pi->Kp * error;

    // Integral part
    pi->integral += pi->Ki * pi->Ts * error;

    // Output voltage
    float u = P + pi->integral;

    // Saturation and anti-windup handling
    if (u > pi->u_max) {
        u = pi->u_max;
        pi->integral -= pi->Ki * pi->Ts * error;
    } else if (u < pi->u_min) {
        u = pi->u_min;
        pi->integral -= pi->Ki * pi->Ts * error;
    }
 
    return u;
}



float converter_model(float u_in) {
    // Function for calculating the DC-converter output voltage. The return value is used to set LED brightness.
    static float i_1 = 0;
    static float i_2 = 0;
    static float i_3 = 0; 
    static float u_1 = 0;
    static float u_2 = 0;
    static float u_3 = 0;

    // Update the states using the given equations
    i_1 = 0.9652*i_1 - 0.0172*u_1 + 0.0057*i_2 - 0.0058*u_2 + 0.0052*i_3 - 0.0251*u_3 + 0.0471*u_in;
    u_1 = 0.7732*i_1 + 0.1252*u_1 + 0.2315*i_2 + 0.0700*u_2 + 0.1282*i_3 + 0.7754*u_3 + 0.0377*u_in;
    i_2 = 0.8278*i_1 - 0.7522*u_1 - 0.0956*i_2 + 0.3299*u_2 - 0.4855*i_3 + 0.3915*u_3 + 0.0404*u_in;
    u_2 = 0.9948*i_1 + 0.2655*u_1 - 0.3848*i_2 + 0.4212*u_2 + 0.3927*i_3 + 0.2899*u_3 + 0.0485*u_in;
    i_3 = 0.7648*i_1 - 0.4165*u_1 - 0.4855*i_2 - 0.3366*u_2 - 0.0986*i_3 + 0.7281*u_3 + 0.0373*u_in;
    u_3 = 1.1056*i_1 + 0.7587*u_1 + 0.1179*i_2 + 0.0748*u_2 - 0.2192*i_3 + 0.1491*u_3 + 0.0539*u_in;
    
    return u_3;
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

    // Enable TIM4 which is used to control PWM for led
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

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

    
    // Initial values for PI controller
    PI_Init(&pi_controller, 1.0, 0.5, 0.5, 0.1, 3.3);
    
    // Start the timer for the converter model and PI controller updates
    TIM3->CNT = 0;
    TIM3->CR1 |= TIM_CR1_CEN;

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
                        lock_acquire();
                        handleToggleButton();

                        if (currentVariable == KP_ACTIVE) {
                            printf("KP is active\r\n");
                        } else {
                            printf("KI is active\r\n");
                        }
                        break;
                    case INCREASE:
                        printf("KP: %f ----- KI: %f \r\n", pi_controller.Kp, pi_controller.Ki);
                        handleIncreaseButton(&pi_controller);
                        break;
                    case REDUCE:
                        printf("KP: %f ----- KI: %f \r\n", pi_controller.Kp, pi_controller.Ki);
                        handleReduceButton(&pi_controller);
                        break;
                }
            }
            buttons[i].button_prev = buttons[i].button_now;
        }
    
        switch(localState) {
            case IDLE:
                // In case of IDLE state, nothing is done and the loop can be continued
                // Converter is in off state.
                break;

            case MODULATE:
                TIM4->CCR1 = (int)(output_voltage * 250);
                break;
            case CONFIG:
                break;
        }
    }
}

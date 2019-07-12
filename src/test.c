#include "stm32f10x.h"


int main(void) {
    {
     RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    }
}

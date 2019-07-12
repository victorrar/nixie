/*
**
**                           Main.c
**
**
**********************************************************************/
/*
   Last committed:     $Revision: 00 $
   Last changed by:    $Author: $
   Last changed date:  $Date:  $
   ID:                 $Id:  $

**********************************************************************/
#include "stm32f10x.h"
#define ALARM_SECONDS 86400
#define FLAG_REGISTER DR1
#define DATE_REGISTER_L DR2
#define DATE_REGISTER_H DR3
#define BKP_PWR_LOSS_FLAG 0x01

#define ZERO_YEAR 1970
//structures
struct Date {
    uint8_t weekDay;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};
struct Time {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};
//global variables

//Functions
void incrementDay(struct Date*); //smart day++ in pointed struct
void writeDate(struct Date);
struct Date readDate(void);
void setTube(uint8_t anode, uint8_t cathode);
void delay(int i){
    while(--i);
}

int main(void) {
    //RCC config
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPAEN; //PORT C
    RCC->APB1ENR |= RCC_APB1ENR_BKPEN;  //Backup domain clock
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;  //Power registers clock



    {
        //quartz GPIO (14, 15) as Alternate Push-Pull 50MHz
        GPIOC->CRH |= GPIO_CRH_CNF14_1;
        GPIOC->CRH &= ~GPIO_CRH_CNF14_0;

        GPIOC->CRH |= GPIO_CRH_CNF15_1;
        GPIOC->CRH &= ~GPIO_CRH_CNF15_0;

        GPIOC->CRH |= GPIO_CRH_MODE14_0 |
                      GPIO_CRH_MODE14_1 |
                      GPIO_CRH_MODE15_0 |
                      GPIO_CRH_MODE15_1;
    }
    PWR->CR |= PWR_CR_DBP;  //Enable access to backup register
    if(!(BKP->FLAG_REGISTER & BKP_PWR_LOSS_FLAG)) { //first launch after BKP power loss
        {
            RCC->BDCR |= RCC_BDCR_BDRST;    //reset backup domain registers
            RCC->BDCR &= ~RCC_BDCR_BDRST;    //reset backup domain registers
        }
        {
            //RTC clock
            RCC->BDCR |= RCC_BDCR_LSEON; //enable LSE
            RCC->BDCR |= RCC_BDCR_RTCSEL_0; //LSE as clock
            RCC->BDCR |= RCC_BDCR_RTCEN; //enable RTC
        }
        {

        }
        {
            //RTC config
            while(!(RTC->CRL & RTC_CRL_RTOFF));   //wait until previous write operation finished
            RTC->CRL |= RTC_CRL_CNF;    //enter configuration mode (for writing RTC_PRL, RTC_CNT, RTC_ALR)

            RTC->PRLH = 0x0000;
            RTC->PRLL = 0x7FFF; // F = 32.768kHz/(0x00007FFF + 1) = 1 Hz


            RTC->ALRH = (ALARM_SECONDS - 1) >> 16;
            RTC->ALRL = (ALARM_SECONDS - 1) >> 0;
            {   //initial time setup
                char* tmpStr = malloc(8);
                memcpy(tmpStr, __TIME__, 8);    //hh:mm:ss
                tmpStr[2] = 0;  //make null terminates strings for each part
                tmpStr[5] = 0;  //hh mm ss
                int h = atoi(tmpStr);
                int m = atoi(&tmpStr[3]);
                int s = atoi(&tmpStr[6]);

                int tmpTime = h*60*60 + //seconds from start of the day
                              m*60 +
                              s;
                free(tmpStr);
                RTC->CNTH = (tmpTime) >> 16;    //load timer
                RTC->CNTL = (tmpTime) >> 0;
            }
            RTC->CRL &= ~RTC_CRL_CNF;    //exit configuration mode, write initialization
            {   //initial date setup

                struct Date *date = malloc(sizeof(struct Date));
                char* tst = __DATE__;
                const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
                            "Sep", "Oct", "Nov", "Dec"};

                char temp [] = __DATE__;
                unsigned char i;

                date->year = atoi(temp + 7);
                temp[6] = 0;
                date->day = atoi(temp + 4);
                temp[3] = 0;
                for (i = 0; i < 12; i++)
                {
                    if (!strcmp(temp, months[i]))
                    {
                        date->month = i + 1;
                    }
                }
                writeDate(*date);    //temp creation date TODO:delete
                free(date);
            }
            while(!(RTC->CRL & RTC_CRL_RTOFF)); //wait for RTC config writing
        }
        BKP->FLAG_REGISTER |= BKP_PWR_LOSS_FLAG;   //non-first launch flag
    }
    while(!(RTC->CRL & RTC_CRL_RTOFF));   //wait until previous write operation finished
    RTC->CRH |= RTC_CRH_SECIE;  //second interrupt enable
    RTC->CRH |= RTC_CRH_ALRIE;  //alarm (day) interrupt enable
    NVIC_EnableIRQ(RTC_IRQn);   //RTC interrupt enable

    while(!(RTC->CRL & RTC_CRL_RSF));   //wait until register sync (once after APB1 clock loss (reset))
    {
        unsigned int time = (RTC->CNTH << 16) | RTC->CNTL;
        if(time >= ALARM_SECONDS) {
            while(!(RTC->CRL & RTC_CRL_RTOFF));   //wait until previous write operation finished
            RTC->CRL |= RTC_CRL_CNF;    //enter configuration mode (for writing RTC_PRL, RTC_CNT, RTC_ALR)

            int dateDelta = time / ALARM_SECONDS;
            struct Date date = readDate();
            while (dateDelta--) {
                incrementDay(&date);
            }
            writeDate(date);
            int truncatedTime = time % ALARM_SECONDS; //truncated time
            RTC->CNTH = truncatedTime >> 16;
            RTC->CNTL = truncatedTime;
            RTC->CRL &= ~RTC_CRL_CNF;    //exit configuration mode, write initialization
        }
    }

    SysTick_Config(72000000 / 1000); //trigger SysTick every millisecond

    {
        //ADC initialization
        RCC->APB2ENR |= RCC_APB2ENR_ADC1EN; //ADC for battery voltage measurement
        ADC1->CR2 |= ADC_CR2_ADON;  //ADC power-up (first time bit set)




    }
    {
        //PWM initialization
        //PA1 TIM2 ch 1
        RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

        GPIOA->CRL |= GPIO_CRL_CNF1_1;
        GPIOA->CRL &= ~GPIO_CRL_CNF1_0;
        GPIOA->CRL |= GPIO_CRL_MODE1_0 | GPIO_CRL_MODE1_1;  //50MHz

        TIM2->ARR = 0x05FF;
        TIM2->CR1 |= TIM_CR1_CEN;

        TIM2->CCMR1 |= TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1; //PWM mode
        TIM2->CCER |= TIM_CCER_CC2E;    //2 channel output enable
        TIM2->CCER |= TIM_CCER_CC2P;    //2 channel inverse polarity
    }
    {   //tube address GPIO
        GPIOA->CRL &= ~(GPIO_CRL_CNF4_0 | GPIO_CRL_CNF4_1);
        GPIOA->CRL &= ~(GPIO_CRL_CNF5_0 | GPIO_CRL_CNF5_1);
        GPIOA->CRL &= ~(GPIO_CRL_CNF6_0 | GPIO_CRL_CNF6_1);
        GPIOA->CRL &= ~(GPIO_CRL_CNF7_0 | GPIO_CRL_CNF7_1);
        GPIOA->CRH &= ~(GPIO_CRH_CNF8_0 | GPIO_CRH_CNF8_1);
        GPIOA->CRH &= ~(GPIO_CRH_CNF9_0 | GPIO_CRH_CNF9_1);
        GPIOA->CRH &= ~(GPIO_CRH_CNF10_0 | GPIO_CRH_CNF10_1);
        GPIOA->CRH &= ~(GPIO_CRH_CNF11_0 | GPIO_CRH_CNF11_1);

        GPIOA->CRL |= GPIO_CRL_MODE4_0;
        GPIOA->CRL |= GPIO_CRL_MODE5_0;
        GPIOA->CRL |= GPIO_CRL_MODE6_0;
        GPIOA->CRL |= GPIO_CRL_MODE7_0;
        GPIOA->CRH |= GPIO_CRH_MODE8_0;
        GPIOA->CRH |= GPIO_CRH_MODE9_0;
        GPIOA->CRH |= GPIO_CRH_MODE10_0;
        GPIOA->CRH |= GPIO_CRH_MODE11_0;
    }


    struct Date date = readDate();  //debug
    GPIOC->CRH |= GPIO_CRH_MODE13_0;    //debug
    GPIOC->CRH |= GPIO_CRH_MODE13_1;    //debug



    TIM2->CCR2 = 0x00FF;   //98% Duty cycle

    GPIOA->CRL |= GPIO_CRL_MODE4_0 | GPIO_CRL_MODE4_1;  //50MHz
    GPIOA->CRL &= ~GPIO_CRL_CNF4_0;  //Push-pull
    GPIOA->CRL &= ~GPIO_CRL_CNF4_1;  //Primary function
    GPIOA->BSRR |= GPIO_BSRR_BS4;   //set 1


    while(1) {
        unsigned int clock = RTC->CNTH << 16 | RTC->CNTL;
        int seconds = clock % 60;
        int minutes = clock / 60 % 60;
        int hours = clock / 60 / 60 % 60;
        //GPIOA->BSRR |= ((seconds % 2) ? GPIO_BSRR_BS4 : GPIO_BSRR_BR4);   //set 1
        //GPIOA->ODR ^= GPIO_ODR_ODR4;
        int i = 1000000;
        while(--i);
    }
}


//IRQ_Handlers
void RTC_IRQHandler() {
    while(!(RTC->CRL & RTC_CRL_RTOFF));   //wait until previous write operation finished

    if(RTC->CRL & RTC_CRL_SECF) {   //second interrupt
        RTC->CRL &= ~ RTC_CRL_SECF; //clear interrupt flag (only SW)
        GPIOC->BSRR |= ((RTC->CNTL % 2) ? GPIO_BSRR_BS13 : GPIO_BSRR_BR13);   //set 1
    }
    if(RTC->CRL & RTC_CRL_ALRF) {   //alarm interrupt
        RTC->CRL &= ~ RTC_CRL_ALRF; //clear interrupt flag (only SW)
        while(!(RTC->CRL & RTC_CRL_RTOFF));   //wait until previous write operation finished
        RTC->CRL |= RTC_CRL_CNF;    //enter configuration mode (for writing RTC_PRL, RTC_CNT, RTC_ALR)
        RTC->CNTH = 0x0000;
        RTC->CNTL = 0x0000; //reset counter
        RTC->CRL &= ~RTC_CRL_CNF;    //exit configuration mode, write initialization
        struct Date date = readDate();
        incrementDay(&date);
        writeDate(date);
    }
}
int tubeIndex = 0;
void SysTick_Handler(void) {    //ticks every 1ms
    //anode addr A4...A7
    //cathode addr A8...A11
    tubeIndex = ++tubeIndex % 6;

    unsigned int clock = RTC->CNTH << 16 | RTC->CNTL;
    int seconds = clock % 60;
    int minutes = clock / 60 % 60;
    int hours = clock / 60 / 60 % 60;
    //543210
    //hhmmss
    uint8_t data[6];
    data[0] = seconds % 10;
    data[1] = (seconds / 10) % 10;
    data[2] = minutes % 10;
    data[3] = (minutes / 10) % 10;
    data[4] = hours % 10;
    data[5] = (hours / 10) % 10;
    setTube(tubeIndex, data[tubeIndex]);

}
//Functions
void inline setTube(uint8_t anode, uint8_t cathode){
    GPIOA->BSRR |= anode << 4 |
                (((~anode << 4) << 16) & (GPIO_BSRR_BR4 | GPIO_BSRR_BR5 | GPIO_BSRR_BR6 | GPIO_BSRR_BR7));
    delay(1);
    GPIOA->BSRR |= cathode << 8 |
                (((~cathode << 8) << 16) & (GPIO_BSRR_BR8 | GPIO_BSRR_BR9 | GPIO_BSRR_BR10 | GPIO_BSRR_BR11));
}


void incrementDay(struct Date *date) {


    switch (date->month) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:    //31 days in month
        if(date->day < 31) {
            date->day++;
            break;
        }
        if(date->month != 12) {
            date->month++;
            date->day = 1;
            break;
        }
        date->month = 1;
        date->day = 1;
        date->year++;
        break;
    case 4:
    case 6:
    case 9:
    case 11:    //30 days in month
        if(date->day < 30) {
            date->day++;
            break;
        }
        date->month++;
        date->day = 1;
        break;
    case 2: //February 28(29) days in month
        if(date->day < 28) {
            date->day++;
            break;
        }
        if((date->day == 28) && (!(date->year % 400) || ((date->year % 100) && !(date->year % 4)))) {   //if feb 28 and leap year
            date->day++;
        }
        date->month++;
        date->day = 1;
        break;
    }
}

void writeDate(struct Date date) {

    //FUCKING MAGIC, source "http://howardhinnant.github.io/date_algorithms.html"
    date.year -= date.month <= 2;
    const int era = (date.year >= 0 ? date.year : date.year-399) / 400;
    const uint16_t yoe = date.year - era * 400;      // [0, 399]
    const uint16_t doy = (153*(date.month + (date.month > 2 ? -3 : 9)) + 2)/5 + date.day-1;  // [0, 365]
    const uint32_t doe = yoe * 365 + yoe/4 - yoe/100 + doy;         // [0, 146096]
    int unix = era * 146097 + doe - 719468;

    BKP->DATE_REGISTER_H = unix >> 16;
    BKP->DATE_REGISTER_L = unix;

}
struct Date readDate(void) {
    struct Date date;
    int days = (BKP->DATE_REGISTER_H << 16) | (BKP->DATE_REGISTER_L);
    //FUCKING MAGIC
    date.weekDay = days >= -3 ? (days+3) % 7 : (days+4) % 7 + 6;
    days += 719468;
    const int era = (days >= 0 ? days : days - 146096) / 146097;
    const uint32_t doe = days - era * 146097;          // [0, 146096]
    const uint16_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
    const int y = yoe + era * 400;
    const uint16_t doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
    const uint16_t mp = (5*doy + 2)/153;                                   // [0, 11]
    date.day = doy - (153*mp+2)/5 + 1;                                     // [1, 31]
    date.month = mp + (mp < 10 ? 3 : -9);                                  // [1, 12]
    date.year = y + (date.month <= 2);
    return date;
}

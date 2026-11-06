
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

// RCC
#define RCC_AHBENR REG32(0x40021014u)

// GPIOE
#define GPIOE_MODER REG32(0x48001000u)
#define GPIOE_BSRR REG32(0x48001018u)

// SysTick
#define SYST_CSR REG32(0xE000E010u)
#define SYST_RVR REG32(0xE000E014u)
#define SYST_CVR REG32(0xE000E018u)

// ADC1
#define ADC1_ISR REG32(0x50000000u)
#define ADC1_CR REG32(0x50000008u)
#define ADC1_CFGR REG32(0x5000000Cu)
#define ADC1_SMPR2 REG32(0x50000018u)
#define ADC1_SQR1 REG32(0x50000030u)
#define ADC1_DR REG32(0x50000040u)

// ADC1/ADC2 common control
#define ADC12_CCR REG32(0x50000308u)

#define CPU_HZ 8000000u
// Set this to the measured ADC reference voltage for better accuracy.
#define ADC_VDDA_MV 3300u
#define TEMP_REFERENCE_ADC 1932u
#define TEMP_REFERENCE_C 21
#define TS_CAL1 (*(const volatile uint16_t *)0x1FFFF7B8u)
#define TS_CAL2 (*(const volatile uint16_t *)0x1FFFF7C2u)
#define ADC_FILTER_MAX_POINTS 64u

static const uint8_t led_pins[8] = {
    13, 14, 15, 8, 9, 10, 11, 12};

void delay_ms(uint32_t ms)
{
    SYST_RVR = (CPU_HZ / 1000u) - 1u;
    SYST_CVR = 0;
    SYST_CSR = (1u << 2) | (1u << 0);

    while (ms--)
    {
        while (!(SYST_CSR & (1u << 16)))
        {
        }
    }

    SYST_CSR = 0;
}

void LED_Configure(void)
{
    RCC_AHBENR |= (1u << 21);

    GPIOE_MODER =
        (GPIOE_MODER & ~0xFFFF0000u) |
        0x55550000u;

    GPIOE_BSRR = 0xFF000000u;
}

void LED_SetByte(uint8_t value)
{
    uint32_t set = 0;
    uint32_t reset = 0;

    for (unsigned bit = 0; bit < 8; ++bit)
    {
        uint32_t pin = 1u << led_pins[bit];

        if (value & (1u << bit))
            set |= pin;
        else
            reset |= pin;
    }

    GPIOE_BSRR = set | (reset << 16);
}

void ADC_Configure(void)
{
    // Enable ADC1/ADC2 peripheral clock.
    RCC_AHBENR |= (1u << 28);

    // Synchronous ADC clock = HCLK / 1.
    // With reset clock configuration: 8 MHz.
    ADC12_CCR = (ADC12_CCR & ~(3u << 16)) | (1u << 16);

    // Exit ADC deep-power-down mode, if enabled.
    ADC1_CR &= ~(1u << 29);

    // Enable ADC internal voltage regulator:
    // ADVREGEN[1:0] = 01.
    ADC1_CR &= ~(3u << 28);
    ADC1_CR |= (1u << 28);

    // Wait for regulator startup.
    delay_ms(1);

    // Single-ended ADC calibration.
    ADC1_CR &= ~(1u << 30);
    ADC1_CR |= (1u << 31);

    while (ADC1_CR & (1u << 31))
    {
    }

    // 12-bit, right-aligned, single conversion,
    // software trigger (reset configuration).
    ADC1_CFGR = 0;

    // ADC channel 16 sampling time:
    // 601.5 ADC clock cycles.
    ADC1_SMPR2 &= ~(7u << 18);
    ADC1_SMPR2 |= (7u << 18);

    // One regular conversion, channel 16.
    ADC1_SQR1 = (16u << 6);

    // Enable internal temperature sensor (TSEN).
    ADC12_CCR |= (1u << 23);

    // Allow temperature sensor to stabilize.
    delay_ms(1);

    // Clear ADC ready flag.
    ADC1_ISR = (1u << 0);

    // Enable ADC.
    ADC1_CR |= (1u << 0);

    // Wait until ADC is ready.
    while (!(ADC1_ISR & (1u << 0)))
    {
    }
}

uint16_t ADC_Read(void)
{
    // Clear end-of-conversion flag.
    ADC1_ISR = (1u << 2);

    // Start regular conversion.
    ADC1_CR |= (1u << 2);

    // Wait for conversion completion.
    while (!(ADC1_ISR & (1u << 2)))
    {
    }

    return (uint16_t)(ADC1_DR & 0x0FFFu);
}

static uint16_t filter_samples[ADC_FILTER_MAX_POINTS];
static uint32_t filter_sum;
static uint16_t filter_points, filter_count, filter_index;

// Reset the filter when changing its window. Returns 0 for an invalid size.
int ADC_FilterSetPoints(uint16_t points)
{
    if (points == 0 || points > ADC_FILTER_MAX_POINTS)
        return 0;

    filter_points = points;
    filter_count = 0;
    filter_index = 0;
    filter_sum = 0;
    return 1;
}

uint16_t ADC_FilterSample(uint16_t sample)
{
    if (filter_points == 0)
        return sample;

    if (filter_count == filter_points)
        filter_sum -= filter_samples[filter_index];
    else
        ++filter_count;

    filter_samples[filter_index] = sample;
    filter_sum += sample;
    if (++filter_index == filter_points)
        filter_index = 0;

    return (uint16_t)(filter_sum / filter_count);
}

// Use the factory slope and a measured reference point to correct the offset.
// The reference ADC reading must be taken at the same VDDA as the measurement.
int32_t ADC_TemperatureC(uint16_t adc)
{
    int32_t cal1 = TS_CAL1;
    int32_t cal2 = TS_CAL2;
    int32_t difference = (int32_t)adc - (int32_t)TEMP_REFERENCE_ADC;

    if (cal1 == cal2)
        return 0; // Invalid calibration data; avoid division by zero.

    return TEMP_REFERENCE_C + (difference * (int32_t)ADC_VDDA_MV * 80) /
                                  (3300 * (cal2 - cal1));
}

int main(void)
{
    LED_Configure();
    ADC_Configure();
    ADC_FilterSetPoints(8);

    for (;;)
    {
        uint16_t filtered_adc = ADC_FilterSample(ADC_Read());
        int32_t temperature = ADC_TemperatureC(filtered_adc);
        if (temperature < -128)
            temperature = -128;
        if (temperature > 127)
            temperature = 127;
        LED_SetByte((uint8_t)temperature);

        delay_ms(100);
    }
}

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include "pid.h"

volatile uint8_t sampleflag = 0;

uint8_t  eeSetpoint EEMEM;
uint8_t  eeOpmode EEMEM;
uint16_t eeP_factor EEMEM;
uint16_t eeI_factor EEMEM;
uint16_t eeD_factor EEMEM;

piddata_t piddata = {0,0,0,0,MANUAL,0,0,0};
#ifdef PID_DEBUG
piddebug_t piddebug = {0,0,0,0};
#endif

ISR(WDT_vect)
{
	// keep WDT from resetting, interrupt instead
	WDTCR |= _BV(WDIE);
    sampleflag = 1;
}

void init_periph()
{
    cli();

    // I/O setup
    IO_PORT = 0x00;
    IO_DDR |=_BV(IO_PWM);

    // PWM setup
    // Timer1, does FAST PWM, prescaler = 1
    TCCR1 =  _BV(CS10);
    GTCCR  = _BV(COM1B0) | _BV(PWM1B);
    OCR1C = 0xFF; // TOP
    PWM = 0;

    // WDT setup
    // enable watchdog timer, use interrupt instead of reset, every 0.016 seconds
    WDTCR = _BV(WDE) | _BV(WDIE);
    
    // ADC setup
    // enable ADC, VCC ref., set prescaler to 8
    ADCSRA = _BV(ADEN) | _BV(ADPS1) | _BV(ADPS0);
    // set ADC channel
    ADMUX = ADCHAN;

    // enable interrupts
    sei();
}

void init_pid()
{
    cli();
    pid_set_output(0);
    pid_load_parameters();
    sei();
}

void pid_run()
{
    piddata.last_pv = piddata.processvalue;
    piddata.processvalue = pid_read_pv();

    if (sampleflag == 1 && piddata.opmode == AUTO) {
		sampleflag = 0;
        pid_contr();
	}
}

void pid_auto()
{
	pid_reset();
	piddata.opmode = AUTO;
}

void pid_manual()
{
	pid_reset();
	piddata.opmode = MANUAL;
}

void pid_reset() 
{
    piddata.esum = 0;
	piddata.last_pv = 0;
    piddata.processvalue = 0;
#ifdef PID_DEBUG
	piddebug.pterm = piddebug.iterm = piddebug.dterm = 0;
	piddebug.e = 0;
#endif
}

void pid_save_parameters()
{
    eeprom_write_word(&eeP_factor, piddata.P_factor);
    eeprom_write_word(&eeI_factor, piddata.I_factor);
    eeprom_write_word(&eeD_factor, piddata.D_factor);
    eeprom_write_byte(&eeSetpoint, piddata.setpoint);
    eeprom_write_byte(&eeOpmode,   piddata.opmode);
}

void pid_load_parameters()
{
    piddata.P_factor = eeprom_read_word(&eeP_factor);
    piddata.I_factor = eeprom_read_word(&eeI_factor);
    piddata.D_factor = eeprom_read_word(&eeD_factor);
    piddata.opmode   = eeprom_read_byte(&eeOpmode);
    piddata.setpoint = eeprom_read_byte(&eeSetpoint);
}

void pid_contr()
{
	int16_t e;
	int32_t pterm, iterm, dterm;
	int64_t u;

    e = piddata.setpoint - piddata.processvalue;
	piddata.esum += e;

    if (piddata.esum > MAX_ERROR_SUM) 
        piddata.esum = MAX_ERROR_SUM;
    else if (piddata.esum < -MAX_ERROR_SUM)
        piddata.esum = -MAX_ERROR_SUM;
	
	// multiplying 32-bit integers with negatives doesn't work well,
	// so there is this little workaround:
	if (e >= 0)
		pterm = piddata.P_factor * e;
	else {
		pterm = piddata.P_factor * -e;
		pterm = -pterm;
	}

	if (piddata.esum >= 0)
		iterm = piddata.I_factor * piddata.esum;
	else {
		iterm = piddata.I_factor * -piddata.esum;
		iterm = -iterm;
	}

    // for more robustness, base D-term to change in process value only (ref.: AVR221)
    if (piddata.processvalue >= piddata.last_pv)
		dterm = piddata.D_factor * (piddata.processvalue - piddata.last_pv);
	else {
		dterm = piddata.D_factor * (piddata.last_pv - piddata.processvalue);
		dterm = -dterm;
	}
    
    u = (pterm + iterm + dterm) / SCALING_FACTOR;
    
    pid_set_output( (int32_t) u);
	
#ifdef PID_DEBUG
	piddebug.pterm = pterm;
	piddebug.iterm = iterm;
	piddebug.dterm = dterm;
	piddebug.e = e;
#endif
}

void pid_set_output(int32_t y)
{
    if (y > MAX_OUTPUT) 
        PWM = MAX_OUTPUT;
    else if (y < MIN_OUTPUT)
        PWM = MIN_OUTPUT;
    else 
        PWM = (uint8_t) y;
}

uint8_t pid_get_output()
{
    return PWM;
}

uint8_t pid_read_pv()
{
    uint16_t a = 0;
	uint8_t i = 0;

    ADMUX = ADCHAN ;
	
	// read ADC, take 4 samples
	for (i=0; i < 4; i++) {
		ADCSRA |= _BV(ADSC);         // start conversion
		while (ADCSRA & _BV(ADSC));  // wait till end of conversion

		a += ADCW;
	}
	a = a >> 2; // devide by 4 to get mean
	a = a >> 2; // get 8bit result
    return (uint8_t) a;
}

#include "I2CAnalogClock.h"

volatile uint16_t position; // This is the position that we believe the clock is in.
volatile uint16_t adjustment;   // This is the adjustment to be made.
volatile uint8_t tp_duration;
volatile uint8_t ap_duration;
volatile uint8_t ap_delay;     // delay in ms between ticks during adjustment

volatile uint8_t control;       // This is our control "register".
volatile uint8_t status;        // status register

volatile uint8_t command;       // This is which "register" to be read/written.

volatile unsigned int receives;
volatile unsigned int requests;
volatile unsigned int errors;

// i2c receive handler
void i2creceive(int size)
{
    ++receives;
    command = Wire.read();
    --size;
    // check for a write command
    if (size > 0)
    {
        switch (command)
        {
        case CMD_POSITION:
            position = Wire.read() | Wire.read() << 8;
            break;
        case CMD_ADJUSTMENT:
            adjustment = Wire.read() | Wire.read() << 8;
            break;
        case CMD_TP_DURATION:
            tp_duration = Wire.read();
            break;
        case CMD_AP_DURATION:
            ap_duration = Wire.read();
            break;
        case CMD_AP_DELAY:
            ap_delay = Wire.read();
            break;
        case CMD_CONTROL:
            control = Wire.read();
            break;
        case CMD_STATUS:
            status = Wire.read();
            break;
        default:
            ++errors;
        }
    }
}

// i2c request handler
void i2crequest()
{
    ++requests;
    uint16_t value;
    switch (command)
    {
    case CMD_POSITION:
        value = position;
        Wire.write(value & 0xff);
        Wire.write(value >> 8);
        break;
    case CMD_ADJUSTMENT:
        value = adjustment;
        Wire.write(value & 0xff);
        Wire.write(value >> 8);
        break;
    case CMD_TP_DURATION:
        value = tp_duration;
        Wire.write(value);
        break;
    case CMD_AP_DURATION:
        value = ap_duration;
        Wire.write(value);
        break;
    case CMD_AP_DELAY:
        value = ap_delay;
        Wire.write(value);
        break;
    case CMD_CONTROL:
        Wire.write(control);
        break;
    case CMD_STATUS:
        Wire.write(status);
        break;
    default:
        ++errors;
    }
}

void (*timer_cb)();
volatile bool timer_running;
volatile unsigned int start_time;
#ifdef DEBUG_TIMER
volatile unsigned int starts;
volatile unsigned int stops;
volatile unsigned int ints;
volatile unsigned int last_duration;
volatile unsigned int stop_time;
#endif

ISR(TIMER1_COMPA_vect)
{
    unsigned int int_time = millis();
#ifdef DEBUG_TIMER
    ints += 1;
#endif

    // why to we get an immediate interrupt? (ignore it)
    if (int_time == start_time)
    {
        return;
    }

#ifdef __AVR_ATtinyX5__
    TIMSK &= ~(1 << OCIE1A); // disable timer1 interrupts as we only want this one.
#else
    digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1);
    TIMSK1 &= ~(1 << OCIE1A); // disable timer1 interrupts as we only want this one.
#endif
    timer_running = false;
    if (timer_cb != NULL)
    {
#ifdef DEBUG_TIMER
        stops+= 1;
        stop_time = int_time;
#endif
        timer_cb();
    }
}

void startTimer(int ms, void (*func)())
{
#ifdef DEBUG_TIMER
    starts += 1;
    last_duration = ms;
#endif
    start_time = millis();
    uint16_t timer = ms2Timer(ms);
    // initialize timer1
    noInterrupts();
    // disable all interrupts
    timer_cb = func;
#ifdef __AVR_ATtinyX5__
    TCCR1 = 0;
    TCNT1 = 0;

    OCR1A = timer;   // compare match register
    TCCR1 |= (1 << CTC1);   // CTC mode
    TCCR1 |= PRESCALE_BITS;
    TIMSK |= (1 << OCIE1A);  // enable timer compare interrupt
    // clear any already pending interrupt?  does not work :-(
    TIFR &= ~(1 << OCIE1A);
#else
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;

    OCR1A = timer;   // compare match register
    TCCR1B |= (1 << WGM12);   // CTC mode
    TCCR1B |= PRESCALE_BITS;
    TIMSK1 |= (1 << OCIE1A);  // enable timer compare interrupt
    // clear any already pending interrupt?  does not work :-(
    TIFR1 &= ~(1 << OCIE1A);
#endif
    timer_running = true;
    interrupts();
    // enable all interrupts
}

void startTick()
{
#ifdef DRV8838
    digitalWrite(DRV_PHASE, isTick());
    digitalWrite(DRV_ENABLE, TICK_ON);
#else
    // toggle the pins.
    digitalWrite(A_PIN, !digitalRead(A_PIN));
#endif
}

void endTick()
{
#ifdef DRV8838
    digitalWrite(DRV_ENABLE, TICK_OFF);
    toggleTick();
#else
    digitalWrite(B_PIN, !digitalRead(B_PIN));
#endif
}

// advance the position
void advancePosition() {
    position += 1;
    if (position >= MAX_SECONDS)
    {
        position = 0;
    }
}

//
//  Advance the clock by one second.
//
void advanceClock(uint16_t duration)
{
    advancePosition();
#ifndef __AVR_ATtinyX5__
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
#endif
    startTick();
    startTimer(duration, &endTick);
}

//
// ISR for 1hz interrupt
//
void tick()
{
    if (isEnabled())
    {

        if (adjustment > 0)
        {
            ++adjustment;
        }
        else
        {
            advanceClock(tp_duration);
        }
    }
}

void setup()
{
#ifdef DEBUG_I2CAC
    Serial.begin(SERIAL_BAUD);
    Serial.println("");
    Serial.println("Startup!");
#endif


    ADCSRA &= ~(1<<ADEN); // Disable ADC as we don't use it, saves ~230uA
    PRR    |= (1<<PRADC); // Turn off ADC clock
    //set_sleep_mode(SLEEP_MODE_PWR_DOWN);

    tp_duration = DEFAULT_TP_DURATION_MS;
    ap_duration = DEFAULT_AP_DURATION_MS;
    ap_delay    = DEFAULT_AP_DELAY_MS;

    //control = BIT_ENABLE;

    Wire.begin(I2C_ADDRESS);
    Wire.onReceive(&i2creceive);
    Wire.onRequest(&i2crequest);

    digitalWrite(B_PIN, TICK_OFF);
    digitalWrite(A_PIN, TICK_OFF);
    pinMode(A_PIN, OUTPUT);
    pinMode(B_PIN, OUTPUT);
    pinMode(INT_PIN, INPUT);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INT_PIN), &tick, FALLING);
}


#ifdef DEBUG_I2CAC
unsigned long last_print;
uint16_t last_pos = -1;
#endif

void loop()
{
    if (adjustment != 0)
    {
    	noInterrupts();
        advancePosition(); // this must be done with interrupts disabled when not in an ISR!
        interrupts();

        startTick();
        delay(ap_duration);
        endTick();
        delay(ap_delay);

    	//
    	// this must be done with interrupts disabled when not in an ISR!
    	//
    	noInterrupts();
    	if (adjustment != 0) {
    		--adjustment;
    	}
    	interrupts();
    }
    else
    {
        sleep_mode();
    }

#ifdef DEBUG_I2CAC
    unsigned long now = millis();
    char buffer[128];
    if ((now - last_print) > 1000) {
        last_print = now;
        if (last_pos != position)
        {
            last_pos = position;
    #ifdef DEBUG_TIMER
            if (timer_running)
            {
                Serial.println("timer running, adding delay!");
                delay(33);
            }
            int last_actual = stop_time - start_time;
            snprintf(buffer, 127, "starts:%d stops: %d ints:%d duration:%d actual:%d\n",
                    starts, stops, ints, last_duration, last_actual);
            Serial.print(buffer);
    #endif
            snprintf(buffer, 127,
                    "position:%d adjustment:%d control:%d seconds:%d\n", position,
                    adjustment, control, position % 60);
            Serial.print(buffer);
        }
#ifdef DEBUG_I2C
        snprintf(buffer, 127, "receives:%d requests:%d errors: %d\n",
                receives, requests, errors);
        Serial.print(buffer);
#endif
    }
#endif
    //delay(1);
}

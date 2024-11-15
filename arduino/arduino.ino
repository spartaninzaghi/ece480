#define NOZZLE_COUNT               12
#define PULSE_DURATION              5  // us
#define PULSE_DELAY_DIFF_NOZZLE     1  // us
#define PULSE_DELAY_SAME_NOZZLE   800  // us

#define DROPS_PER_INCH 96
#define INCHES_PER_REV 1.5748 // 40 mm per revolution
#define STEPS_PER_REV 3200    // 200 full steps * 16 microsteps
#define STEP_SPEED 300
#define HOMING_SPEED 800
#define SWITCH_TIME 100
#define X_OFFSET 100
#define Y_OFFSET 0

enum CommandMode {
  INSTRUCTION,
  MASK1,
  MASK2,
};

struct StepperMotor {
  const int dirPin;
  const int stepPin;
  const int limitPin;
};

struct PinInfo {
  volatile uint8_t* ddr;
  volatile uint8_t* pin;
  volatile uint8_t* port;
  uint8_t bit;
};

const PinInfo unoPins[] = {
//                            GPIO    PIN
  {&DDRD, &PIND, &PORTD, 0}, // D0     0   Rx     
  {&DDRD, &PIND, &PORTD, 1}, // D1     1   Tx
  {&DDRD, &PIND, &PORTD, 2}, // D2     2   
  {&DDRD, &PIND, &PORTD, 3}, // D3     3   
  {&DDRD, &PIND, &PORTD, 4}, // D4     4   
  {&DDRD, &PIND, &PORTD, 5}, // D5     5   
  {&DDRD, &PIND, &PORTD, 6}, // D6     6   
  {&DDRD, &PIND, &PORTD, 7}, // D7     7   
  {&DDRB, &PINB, &PORTB, 0}, // B0     8   
  {&DDRB, &PINB, &PORTB, 1}, // B1     9   
  {&DDRB, &PINB, &PORTB, 2}, // B2     10   
  {&DDRB, &PINB, &PORTB, 3}, // B3     11   
  {&DDRB, &PINB, &PORTB, 4}, // B4     12  
  {&DDRB, &PINB, &PORTB, 5}, // B5     13  on-board LED    
  {&DDRC, &PINC, &PORTC, 0}, // C0     14
  {&DDRC, &PINC, &PORTC, 1}, // C1     15   
  {&DDRC, &PINC, &PORTC, 2}, // C2     16   
  {&DDRC, &PINC, &PORTC, 3}, // C3     17   
  {&DDRC, &PINC, &PORTC, 4}, // C4     18   
  {&DDRC, &PINC, &PORTC, 5}, // C5     19   
};

// Initial command mode
CommandMode cmdMode = INSTRUCTION;

// Nozzle Number       1  2  3  4  5  6  7  8  9   10  11  12 
const int nozzles[] = {14, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};

// Steppers
const StepperMotor motorX = {15, 16, 13};
const StepperMotor motorY = {17, 18, 19};
const int stepsPerDrop = (1.0 / DROPS_PER_INCH) * (1.0 / INCHES_PER_REV) * STEPS_PER_REV;
int posX = 0;

/**
 * Write to a pin using direct port manipulation for low latency
 * @param pin The pin to write
 * @param state 1 (HIGH) to set pin to 5V, 0 (LOW) to set pin to 0V
 */
void digitalWriteFast(const PinInfo& pin, bool state) {
  volatile uint8_t portRegister = *pin.port;

  if (state) {
    portRegister |= 1 << pin.bit;
  } else {
    portRegister &= ~(1 << pin.bit);
  }
  
  *pin.port = portRegister;
}

/**
 * Pulse specified pin for PULSE_DURATION us
 * @param pin The pin to pulse
 */
void pulse(const PinInfo& pin) {
  digitalWriteFast(pin, HIGH); 
  delayMicroseconds(PULSE_DURATION);
  digitalWriteFast(pin, LOW);
}

/**
 * Initiate a pulse on all nozzles sequentially
 *
 * For HP6602, A pulse lasts for 5us, after which follows a delay. 
 * To successively pulse the same nozzle, an ~800us is required. 
 * For different nozzles, a ~0.5us delay is required. 1us is used. See
 * Inkshield theory: http://nicholasclewis.com/projects/inkshield/theory/ 
 */
void pulseTestSuccessive() {
  for (int i = 0; i < NOZZLE_COUNT; i++) {
    pulse(unoPins[nozzles[i]]);
    delayMicroseconds(PULSE_DELAY_DIFF_NOZZLE);
  }
  delayMicroseconds(PULSE_DELAY_SAME_NOZZLE);
}

/** 
 * Move stepper motor by specified amount
 * @param motor The motor to drive
 * @param stepCount Number of 1/16 microsteps to take
*/
void step(const StepperMotor& motor, int stepCount, int speed) {
  if(stepCount >= 0) {
    digitalWriteFast(unoPins[motor.dirPin], LOW);
  } else {
    digitalWriteFast(unoPins[motor.dirPin], HIGH);
  }
  for(int i = 0; i < abs(stepCount); i++) {
    digitalWriteFast(unoPins[motor.stepPin], HIGH);
    delayMicroseconds(speed);
    digitalWriteFast(unoPins[motor.stepPin], LOW);
    delayMicroseconds(speed);
  }
}

/**
 * Move along x axis by one drop width
 */
void stepDropX() {
  step(motorX, stepsPerDrop, STEP_SPEED);
  posX += stepsPerDrop;
}

/**
 * Return to initial location on x axis
 */
void gotoBeginLine() {
  step(motorX, -posX, STEP_SPEED);
  posX = 0;
}

/**
 * Move printhead to next line (printhead moving in positive Y)
 * Stepper Y controls bed, so it needs to move in negative Y
 */
void gotoNextLine() {
  step(motorY, -stepsPerDrop * NOZZLE_COUNT, STEP_SPEED);
}


/**
 * Move motor to home position
 * @param motor The motor to drive
 */
void homeMotor(const StepperMotor &motor){
  while(digitalRead(motor.limitPin)) {             // assuming switch is wired so that not contacted = true
    step(motor, -1, STEP_SPEED);

    // don't need full debounce, just ensure switch state is maintained before exiting
    if(!digitalRead(motor.limitPin)) {
      delayMicroseconds(SWITCH_TIME);
    }
  }
  // ease off switch at slower speed
  while(!digitalRead(motor.limitPin)) {
    step(motor, 1, HOMING_SPEED);
    if(digitalRead(motor.limitPin)) {
      delayMicroseconds(SWITCH_TIME);
    }
  }
}

void homeSystem() {
  //homeMotor(motorX);
  //step(motorX, X_OFFSET, HOMING_SPEED);
  homeMotor(motorY);
  step(motorY, Y_OFFSET, HOMING_SPEED);
}

// dispense ink based on bitmask
// when last mask done, call stepDropX() and change cmdMode to INSTRUCTION
void dispense(unsigned char mask) {
  if (cmdMode == MASK1) {
    for (int i = 0; i < 8; i++) {
      // pulse nozzle at index `i` if its bit is set HIGH in mask
      if (mask & (1 << i)) {
        pulse(unoPins[nozzles[i]]);
        delayMicroseconds(PULSE_DELAY_DIFF_NOZZLE);
      }
    }
    cmdMode = MASK2;
  }
  else if (cmdMode == MASK2) {
    for (int i = 0; i < 4; i++) {
      if (mask & (1 << i)) {
        // pulse nozzle at index `i + 8` for remaining 4 nozzles
        pulse(unoPins[nozzles[i + 8]]);
        delayMicroseconds(PULSE_DELAY_DIFF_NOZZLE);
      }
    }
    delayMicroseconds(PULSE_DELAY_SAME_NOZZLE);

    cmdMode = INSTRUCTION;
    stepDropX();
  }
}

void setup() {
  // Nozzle pin setup
  for(int i = 0; i < NOZZLE_COUNT; i++) {
    pinMode(nozzles[i], OUTPUT);
  }

  // Stepper pin setup
  pinMode(motorX.dirPin, OUTPUT);
  pinMode(motorX.stepPin, OUTPUT);
  pinMode(motorY.dirPin, OUTPUT);
  pinMode(motorY.stepPin, OUTPUT);
  pinMode(motorX.limitPin, INPUT_PULLUP);
  pinMode(motorY.limitPin, INPUT_PULLUP);
  
  // Serial setup
  Serial.begin(115200);
  while (Serial.available() <= 0) {
    Serial.print('X');
    delay(100);
  }
}

void loop() {
  if (Serial.available() > 0) {
    unsigned char cmd = Serial.read();
    Serial.print('R'); // let host know byte has been received
    if (cmdMode == INSTRUCTION) {
      switch (cmd) {
        case 'D':
          cmdMode = MASK1;
          break;
        case 'R':
          gotoBeginLine();
          break;
        case 'N':
          gotoNextLine();
          break;
        case 'T': 
          pulseTestSuccessive();
          break;
        case 'H':
          homeSystem();
          break;
        default:
          break;
      }
    } else {
      dispense(cmd);
    }
  }
}

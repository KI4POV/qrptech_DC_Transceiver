#include <Wire.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h> // https://codeload.github.com/johnrickman/LiquidCrystal_I2C/zip/master
#include <si5351.h> // https://github.com/NT7S/Si5351

const uint32_t bandStart40 = 7000000;       // start of 40m
const uint32_t bandEnd40   = 7150000;       // end of 40m
const uint32_t bandInit    = 7025000;       // where to initially set the frequency
volatile long oldfreq = 0;
volatile long currentfreq = 0;
volatile long freq40 = 7025000;
volatile int updatedisplay = 0;


// This is the amount of offset between freq (TX freqency) and RXfreq (RX frequency)
// It will change if you implement RIT. If you do not implement RIT, set it to whatever sidetone you like
// to listen to.
volatile uint32_t foffset = 700;   
volatile uint32_t oldfoffset = 1;        

volatile uint32_t freq = bandInit;     // this is a variable (changes) - set it to the beginning of the band
volatile uint32_t RXfreq = freq + foffset;
volatile uint32_t radix = 100;         // how much to change the frequency by, clicking the rotary encoder will change this.
volatile uint32_t oldradix = 1;

// Rotary encoder pins
static const int rotBPin = 2;
static const int rotAPin = 3;

//Setup buttons for front panel
static const int pushPin = 5;   //Controls Radix, or step size when tuning
static const int RIT = 6;       //Controls RIT on/off

//Setup Key
static const int key = 7;
int keyState = 0;

// Rotary encoder variables, used by interrupt routines
volatile int rotState = 0;
volatile int rotAval = 1;
volatile int rotBval = 1;

// Instantiate the Objects
LiquidCrystal_I2C lcd(0x27, 16, 2);
Si5351 si5351;

void setup()
{
  
  // Set up frequency, radix, RIT, and key
  pinMode(rotAPin, INPUT_PULLUP);
  pinMode(rotBPin, INPUT_PULLUP);
  pinMode(pushPin, INPUT_PULLUP);
  pinMode(RIT, INPUT_PULLUP);
  pinMode(key, INPUT_PULLUP);

  // Set up interrupt pins
  attachInterrupt(digitalPinToInterrupt(rotAPin), ISRrotAChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(rotBPin), ISRrotBChange, CHANGE);

  // Initialize the display
  lcd.init();
  lcd.backlight();
  UpdateDisplay();
  delay(1000);

  // Initialize the DDS
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  si5351.set_correction(31830, SI5351_PLL_INPUT_XO);      // Set to specific Si5351 calibration number
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
  si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_8MA);
  si5351.set_freq((RXfreq * 100ULL), SI5351_CLK2);
}


void loop()
{

   // Keying the VFO
  
  keyState = digitalRead(key);
  if (keyState == LOW) {
//    delay(20);                                                              // Optional delay for relay T/R switching.
    si5351.output_enable(SI5351_CLK2, 0);              // Disable RX Clock to mute 
   delay(20);                                                               // move delay here  
    si5351.output_enable(SI5351_CLK0, 1);              // Enable  TX Clock
    si5351.set_freq((freq * 100ULL), SI5351_CLK0); // Set TX Clock Frequency   
  }
   else {
    si5351.output_enable(SI5351_CLK0, 0);             // Disable TX Clock
   delay(20);                                                              // add delay    
    si5351.output_enable(SI5351_CLK2, 1);             // and enable RX Clock
  }

  currentfreq = getfreq();                  // Interrupt safe method to get the current frequency

  if (currentfreq != oldfreq)
  {
    UpdateDisplay();
    SendFrequency();
    oldfreq = currentfreq;
  }
  
  if (digitalRead(pushPin) == LOW)
  {
    delay(10);
    while (digitalRead(pushPin) == LOW)
    {
      if (updatedisplay == 1)
      {
        UpdateDisplay();
        updatedisplay = 0;
      }
    }
    delay(50);
  }
  if (foffset != oldfoffset)
  {
    UpdateDisplay();
    SendFrequency();
    oldfoffset = foffset;
  }
}


long getfreq()
{
  long temp_freq;
  cli();
  temp_freq = freq;
  sei();
  return temp_freq;
}



// Interrupt routines
void ISRrotAChange()
{
  if (digitalRead(rotAPin))
  {
    rotAval = 1;
    UpdateRot();
  }
  else
  {
    rotAval = 0;
    UpdateRot();
  }
}


void ISRrotBChange()
{
  if (digitalRead(rotBPin))
  {
    rotBval = 1;
    UpdateRot();
  }
  else
  {
    rotBval = 0;
    UpdateRot();
  }
}

void UpdateRot()
{
  switch (rotState)
  {

    case 0:                                         // Idle state, look for direction
      if (!rotBval)
        rotState = 1;                               // CW 1
      if (!rotAval)
        rotState = 11;                              // CCW 1
      break;

    case 1:                                         // CW, wait for A low while B is low
      if (!rotBval)
      {
        if (!rotAval)
        {
          // either increment radix, RIT, or main freq adjust
          if (digitalRead(pushPin) == LOW)
          {
            updatedisplay = 1;
            radix = radix * 10;
            if (radix > 100000)
              radix = 100000;
          }
          
          else if (digitalRead(RIT) == LOW)
          {
            foffset = foffset + 10;
          }
          
          
          else
          {
            freq = freq + radix;
            if (freq > bandEnd40)
              freq = bandEnd40;
            
          }
          rotState = 2;                             // CW 2
        }
      }
      else if (rotAval)
        rotState = 0;                               // It was just a glitch on B, go back to start
      break;

    case 2:                                         // CW, wait for B high
      if (rotBval)
        rotState = 3;                               // CW 3
      break;

    case 3:                                         // CW, wait for A high
      if (rotAval)
        rotState = 0;                               // back to idle (detent) state
      break;

    case 11:                                        // CCW, wait for B low while A is low
     if (!rotAval)
      {
        if (!rotBval)
        {
          // either decrement radix, RIT, or main freq adjust
          if (digitalRead(pushPin) == LOW)
          {
            updatedisplay = 1;
            radix = radix / 10;
            if (radix < 1)
              radix = 1;
          }
          
          else if (digitalRead(RIT) == LOW)
          { 
            foffset = foffset - 10;
          }
          
        
          else
          {
            freq = freq - radix;
            if (freq < bandStart40)
              freq = bandStart40;
            
          }
          rotState = 12;                            // CCW 2
        }
      }
      else if (rotBval)
        rotState = 0;                               // It was just a glitch on A, go back to start
      break;

    case 12:                                        // CCW, wait for A high
      if (rotAval)
        rotState = 13;                              // CCW 3
      break;

    case 13:                                        // CCW, wait for B high
      if (rotBval)
        rotState = 0;                               // back to idle (detent) state
      break;
  }
}
        

void UpdateDisplay()
{
  lcd.setCursor(0, 0);
  lcd.setCursor(0, 0);
  lcd.print(freq);
  lcd.setCursor(0, 1);
  
 
  if (radix != oldradix)                          // stops radix display flashing/blinking on freq change
  {
    lcd.setCursor(9, 0);
    lcd.print("       ");
    lcd.setCursor(9, 0);
    if (radix == 1)
      lcd.print("   1 Hz");
    if (radix == 10)
      lcd.print("  10 Hz");
    if (radix == 100)
      lcd.print(" 100 Hz");
    if (radix == 1000)
      lcd.print("  1 kHz");
    if (radix == 10000)
      lcd.print(" 10 kHz");
    if (radix == 100000)
      lcd.print("100 kHz");
    oldradix = radix;
  }
  if (foffset != oldfoffset)
  {
  lcd.print("RIT");
  lcd.setCursor(5, 1);
  lcd.print("     ");
  lcd.setCursor(5, 1);
  lcd.print(foffset);
  }
}

void SendFrequency()
{
  RXfreq = freq + foffset;
  si5351.set_freq((RXfreq * 100ULL), SI5351_CLK2);
} 

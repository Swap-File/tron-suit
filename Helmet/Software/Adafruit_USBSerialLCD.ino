/***********************************
 * Matrix-orbitalish compatible LCD driver with USB and Serial 
 * For use with Teensy 1.0 core on AT90USB162 chips
 * 
 * ---> http://www.adafruit.com/category/63_96
 * 
 * Adafruit invests time and resources providing this open source code, 
 * please support Adafruit and open-source hardware by purchasing 
 * products from Adafruit!
 * 
 * Written by Limor Fried/Ladyada  for Adafruit Industries.  
 * BSD license, check license.txt for more information
 * All text above must be included in any redistribution
 ****************************************/


#include <LiquidCrystalFast.h>
#include <EEPROM.h>
#include <util/delay.h>

// uncomment below to have the display buffer echo'd to USB serial (handy!)
//#define USBLCDDEBUG 1

// this will echo a 'buffer' showing what the display thinks is showing
//#define USBECHOBUFFER 1

// this will echo incoming chars
//#define USBECHOCHARS 1

#define D4  1  // PD1
#define D5  4  // PD4
#define D6  5  // PD5
#define D7  6  // PD6
#define RS  12 // PB4
#define RW  13 // PB5
#define EN  14 // PB6

LiquidCrystalFast lcd(RS, RW, EN, D4, D5, D6, D7);

// This line defines a "Uart" object to access the serial port
HardwareSerial Uart = HardwareSerial();

// connect these to the analog output (PWM) pins!
#define REDLITE 0              // D0
#define GREENLITE 18           // C5
#define BLUELITE 17            // C6
#define CONTRASTPIN 15         // B7

#define GPO_1  8 // PB0
#define GPO_2  20 // PC2
#define GPO_3  19 //PC4
#define GPO_4  16 //PC7


/***** GPO commands */
#define SET_ALL 0xD2 // 3 args - R G B + 80 args (char)

#define START_COMMAND 0x11
//#define END_COMMAND 0x9A

byte SpecialChar0[8]={
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111
};

//Custom Character #1
byte SpecialChar1[8]={
  B11101,
  B11111,
  B01111,
  B11111,
  B11111,
  B11110,
  B11111,
  B10111
};

//Custom Character #2
byte SpecialChar2[8]={
  B10101,
  B11011,
  B01111,
  B10111,
  B11101,
  B11110,
  B11011,
  B10101
};

//Custom Character #3
byte SpecialChar3[8]={
  B10101,
  B11010,
  B01101,
  B10110,
  B01101,
  B10110,
  B01011,
  B10101
};

//Custom Character #4
byte SpecialChar4[8]={
  B01010,
  B00101,
  B10010,
  B01001,
  B10010,
  B01001,
  B10100,
  B01010
};

//Custom Character #5
byte SpecialChar5[8]={
  B01010,
  B00100,
  B10000,
  B01000,
  B00010,
  B00001,
  B00100,
  B01010
};

//Custom Character #6
byte SpecialChar6[8]={
  B00010,
  B00000,
  B10000,
  B00000,
  B00000,
  B00001,
  B00000,
  B01000
};

//Custom Character #7
byte SpecialChar7[8]={
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000
};



unsigned long fpstime=0;
int fps=0;

//serial buffers
byte serialbuffer[90];
int  serialbufferpointer = 0;


void setup() {


  pinMode(CONTRASTPIN, OUTPUT);
  pinMode(REDLITE, OUTPUT);
  pinMode(GREENLITE, OUTPUT);
  pinMode(BLUELITE, OUTPUT);


  pinMode(GPO_1, OUTPUT);
  digitalWrite(GPO_1, HIGH);
  pinMode(GPO_2, OUTPUT);
  digitalWrite(GPO_2, HIGH);
  pinMode(GPO_3, OUTPUT);
  digitalWrite(GPO_3, HIGH);
  pinMode(GPO_4, OUTPUT);
  digitalWrite(GPO_4, HIGH);


  setBacklight(255, 0, 0);
  _delay_ms(250);
  setBacklight(0, 255, 0);
  _delay_ms(250);
  setBacklight(0, 0, 255);
  _delay_ms(250);  
  setBacklight(255, 255, 255);      // set backlight full on

  lcd.createChar(0, SpecialChar7);
  lcd.createChar(1, SpecialChar6);
  lcd.createChar(2, SpecialChar5);
  lcd.createChar(3, SpecialChar4);
  lcd.createChar(4, SpecialChar3);
  lcd.createChar(5, SpecialChar2);
  lcd.createChar(6, SpecialChar1);
  lcd.createChar(7, SpecialChar0);


  // for the initial 'blink' we want to use default settings:
  lcd.begin(20, 4);
  analogWrite(CONTRASTPIN, 0);
  analogWrite(REDLITE, 255);
  analogWrite(GREENLITE, 255);
  analogWrite(BLUELITE, 255);


  lcd.clear();
  lcd.home();


  lcd.print(F("115200 baud 20x4"));
  lcd.setCursor(0,1);
  lcd.print(F("Constrast = 255")); 
  // lcd.print(getContrast());

  _delay_ms(250);
  _delay_ms(250);
  _delay_ms(250);
  _delay_ms(250);

  Serial.begin(115200); 
  Uart.begin(115200);
  // the Uart is a little noisy without a pullup, so we'll use the internal one on PD2
  digitalWrite(PD2, HIGH);



  lcd.clear();
  // do splash screen
  lcd.setCursor(0,0);

  lcd.print(F("SELF TEST PASS"));
  lcd.setCursor(0,2);
  lcd.print(F("ENTERING SLEEP MODE"));

  _delay_ms(250);
  _delay_ms(250);
  _delay_ms(250);
  _delay_ms(250);
  lcd.clear();
  analogWrite(REDLITE, 0);
  analogWrite(GREENLITE, 0);
  analogWrite(BLUELITE, 0);

}

void loop() {


  //if (millis() - fpstime > 1000){
  //   Serial.println(fps);
  //   fps=0;

  //   fpstime=millis();
  // }

  while(Uart.available() || Serial.available() ){

    if (Serial.available()){
      serialbuffer[serialbufferpointer] = Serial.read(); //load a character
    }
    else{
      serialbuffer[serialbufferpointer] = Uart.read(); //load a character
    }

    if (serialbuffer[0] != START_COMMAND){
      serialbufferpointer=-1;
    }

    if (serialbufferpointer == 84){//finished reading
      analogWrite(REDLITE, serialbuffer[1]);
      analogWrite(GREENLITE, serialbuffer[2]);
      analogWrite(BLUELITE,serialbuffer[3]);

      lcd.setCursor(0,0);
      for(int i=4; i<84;i++){
        lcd.write(serialbuffer[i]);
      }
      setGPIO(serialbuffer[84]);

    }

    serialbufferpointer++;

    if (serialbufferpointer>84){
      serialbufferpointer=0;
    }
  }
}




void setBacklight(uint8_t r, uint8_t g, uint8_t b) {

  analogWrite(REDLITE, r);
  analogWrite(GREENLITE, g);
  analogWrite(BLUELITE, b);

  //TCNT0 = 127;
  // TCNT1 = 0;
  // TCCR0B = 0x01;
  // TCCR1B = 0x01;
}

void setGPIO(uint8_t i) {
  if ((i & B00000001) == B00000001){
    digitalWrite(GPO_1, LOW);
  }
  else{
    digitalWrite(GPO_1, HIGH);
  }
  if ((i & B00000010 )== B00000010){
    digitalWrite(GPO_2, LOW);
  }
  else{
    digitalWrite(GPO_2, HIGH);
  }

  if ((i & B00000100) == B00000100){
    digitalWrite(GPO_3, LOW);
  }
  else{
    digitalWrite(GPO_3, HIGH);
  }
  if ((i & B00001000) == B00001000){
    digitalWrite(GPO_4, LOW);
  }
  else{
    digitalWrite(GPO_4, HIGH);
  }
}
































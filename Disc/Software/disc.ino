
#include <TimerOne.h>
#include "LPD8806.h"
#include "SPI.h"
#include <Wire.h>
#include <ADXL345.h>
#include <MovingAverage2.h>

#define spanrange 64 //half range of the span gesture

// Example to control LPD8806-based RGB LED Modules in a strip

/*****************************************************************************/

// Number of RGB LEDs in strand:
int nLEDs = 16;

// Chose 2 pins for output; can be any valid output pins:
const int ClockPin = 13;
const int DataPin = 12;



// First parameter is the number of LEDs in the strand.  The LED strips
// are 32 LEDs per meter but you can extend or cut the strip.  Next two
// parameters are SPI data and clock pins:
LPD8806 strip = LPD8806(16, DataPin, ClockPin);
ADXL345 adxl;
// You can optionally use hardware SPI for faster writes, just leave out
// the data and clock pin parameters.  But this does limit use to very
// specific pins on the Arduino.  For "classic" Arduinos (Uno, Duemilanove,
// etc.), data = pin 11, clock = pin 13.  For Arduino Mega, data = pin 51,
// clock = pin 52.  For 32u4 Breakout Board+ and Teensy, data = pin B2,
// clock = pin B1.  For Leonardo, this can ONLY be done on the ICSP pins.
//LPD8806 strip = LPD8806(nLEDs);

//disc
#define SET_COLOR 0x11
int last_set_color=0;
#define SET_SPAN 0x13
int last_set_span=0;

#define SET_FADE_BRIGHTNESS 0x15
byte last_set_brightness=127;
byte last_set_fade=0;

//these two can overlap
#define SET_RAINBOW 0x19  //no confirmation needed
#define TRIPLE_TAP 0x19  //no sending needed

#define BATTERY_LEVEL 0x1B  

//serial buffers
byte serialbuffer[3];
byte serialbufferpointer = 0;


//index for strip  global for the rainbow effects
int i = 0;
int rainbowoffset=383;



#define S0     6
#define S1     7
#define S2     4
#define S3     5
#define OUT    2
#define LIGHTS 3


volatile int g_count = 0;    // count the frequecy
int   g_array[3];     // store the RGB value
int   g_flag = 0;     // filter of RGB queue


int color = 0;
byte fade = 0;
byte brightness = 127;
int currentmode = 0;
int span = 0;
int instantspan = 0;
int magnitude=8;
int angle=0;


int disc_voltage=1024;
unsigned long lastupdate=0;
unsigned long batteryupdate=0;
unsigned long idle_timer=0;
int previous_x_absolute = 0;
int previous_y_absolute = 0;
int previous_z_absolute = 0;
int x_filtered = 0;
int y_filtered = 0;
int z_filtered = 0;

int xy_magnitude =0;
int xy_filtered_magnitude =0;
int xy_angle = 0;
int xy_filtered_angle = 0;
int previous_xy_magnitude =0;
int previous_xy_filtered_magnitude =0;
int previous_xy_angle = 0;
int previous_xy_filtered_angle = 0;

unsigned long swipetime=0;

int z_latch_color = 0;  //color at time of first latch
int z_latch_angle = -1; //angle of first latch -1 means hasnt occured yet
unsigned long z_latch_time_cooldown = 0; //time of first latch for input debounce
byte z_latch = 1; //keeps track of latching status
byte rotation_status  =0;  
int z_latch_span;
unsigned long tap_time = 0; //time of first latch for input debounce
//overlay
int overlaytimer = 0;  //blinks the LED for modes


// Init TSC230 and setting Frequency.
void TSC_Init()
{
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(OUT, INPUT);
}

// Select the filter color
void TSC_FilterColor(int Level01, int Level02)
{
  digitalWrite(S2, Level01);
  digitalWrite(S3, Level02);
}

void TSC_Count()
{
  g_count++ ;
}

void TSC_Callback()
{
  switch(g_flag)
  {
  case 0:
    g_flag ++; //lets the timer warm up dont use first cycle of timer 1
    digitalWrite(S0, HIGH);  // enable camera at full frequency
    digitalWrite(S1, HIGH);
    break;
  case 1:
    TSC_WB(LOW, LOW);              // set camera to Red
    break;
  case 2:
    g_array[0] = g_count;          // save Red data
    TSC_WB(HIGH, HIGH);            // set camera to Green
    break;
  case 3:
    g_array[1] = g_count;          // save Green data
    TSC_WB(LOW, HIGH);             // set camera to Blue
    break;
  case 4:
    g_array[2] = g_count;          // save Blue data
    Timer1.detachInterrupt();    
    detachInterrupt(0);
    digitalWrite(S0, LOW);  //shut down camera
    digitalWrite(S1, LOW);
    g_flag = 0;
    break;
  }
}

void TSC_WB(int Level0, int Level1) 
{
  g_count = 0;
  g_flag ++;
  TSC_FilterColor(Level0, Level1);
}

void setup()
{
  TSC_Init();
  Serial.begin(115200);
  Timer1.initialize(250000);  
  strip.begin();

  //Color sensor flashlights
  pinMode(LIGHTS, OUTPUT);
  digitalWrite(LIGHTS, LOW);

  Wire.begin();
  adxl.init(0x53);
  adxl.setRangeSetting(4);
  //set activity/ inactivity thresholds (0-255)
  adxl.setActivityThreshold(40); //62.5mg per increment
  adxl.setInactivityThreshold(20); //62.5mg per increment
  adxl.setTimeInactivity(20); // how many seconds of no activity is inactive?

  //look of activity movement on this axes - 1 == on; 0 == off
  adxl.setActivityX(1);
  adxl.setActivityY(1);
  adxl.setActivityZ(1);

  //look of inactivity movement on this axes - 1 == on; 0 == off
  adxl.setInactivityX(1);
  adxl.setInactivityY(1);
  adxl.setInactivityZ(1);

  //look of tap movement on this axes - 1 == on; 0 == off
  adxl.setTapDetectionOnX(1);
  adxl.setTapDetectionOnY(1);
  adxl.setTapDetectionOnZ(1);

  //set values for what is a tap, and what is a double tap (0-255)
  adxl.setTapThreshold(80); //62.5mg per increment
  adxl.setTapDuration(15); //625μs per increment
  adxl.setDoubleTapLatency(80); //1.25ms per increment
  adxl.setDoubleTapWindow(200); //1.25ms per increment

  //set values for what is considered freefall (0-255)
  adxl.setFreeFallThreshold(7); //(5 - 9) recommended - 62.5mg per increment
  adxl.setFreeFallDuration(45); //(20 - 70) recommended - 5ms per increment

  //setting all interupts to take place on int pin 1
  //I had issues with int pin 2, was unable to reset it
  adxl.setInterruptMapping( ADXL345_INT_SINGLE_TAP_BIT, ADXL345_INT1_PIN );
  adxl.setInterruptMapping( ADXL345_INT_DOUBLE_TAP_BIT, ADXL345_INT1_PIN );
  adxl.setInterruptMapping( ADXL345_INT_FREE_FALL_BIT, ADXL345_INT1_PIN );
  adxl.setInterruptMapping( ADXL345_INT_ACTIVITY_BIT, ADXL345_INT1_PIN );
  adxl.setInterruptMapping( ADXL345_INT_INACTIVITY_BIT, ADXL345_INT1_PIN );

  //register interupt actions - 1 == on; 0 == off
  adxl.setInterrupt( ADXL345_INT_SINGLE_TAP_BIT, 0);
  adxl.setInterrupt( ADXL345_INT_DOUBLE_TAP_BIT, 1);
  adxl.setInterrupt( ADXL345_INT_FREE_FALL_BIT, 0);
  adxl.setInterrupt( ADXL345_INT_ACTIVITY_BIT, 1);
  adxl.setInterrupt( ADXL345_INT_INACTIVITY_BIT, 1);

  //populate starting values
  adxl.readAccel(&previous_x_absolute, &previous_y_absolute, &previous_z_absolute); 
} 

void loop()
{

  if (millis()- idle_timer > 10000){
    z_latch = 1;
    currentmode=0;
  }

  //battery level check
  //650 = 9.45v
  disc_voltage = disc_voltage * .97 + analogRead(0) * .03;

  if (disc_voltage < 650){
    for(int i=0; i<strip.numPixels(); i++) {
      if (i%2){
        strip.setPixelColor(i,Wheel(0)); // Set new pixel 'on'
      }
      else{
        strip.setPixelColor(i,0); // Set new pixel 'on'
      }
      strip.showCompileTime<ClockPin, DataPin>();          
      delay(50);
    }
  }

  if (millis() - batteryupdate > 1000){
    sendbattery();
  }

  byte intsource = adxl.getInterruptSource();
  if ((intsource >> ADXL345_INT_DOUBLE_TAP_BIT) & 1)  {
    if (millis() - tap_time < 200 ){
      Serial.write(TRIPLE_TAP);
      for(int i=0; i<strip.numPixels(); i++) {
        strip.setPixelColor(i,0);
      }
      strip.showCompileTime<ClockPin, DataPin>();          
      delay(100);
    }
    else{
      fade=0;
      span=0;
      instantspan=0;
      z_latch = 1;

      //send a heartbeat before animating
      sendbattery();

      for(int i=0; i<16; i++) {
        //start at top of disc and work around
        strip.setPixelColor((i+xy_angle+8 )%16,Wheel(384)); // Set new pixel 'on'
        strip.showCompileTime<ClockPin, DataPin>();              // Refresh LED states
        delay(50);
      }

      //send a heartbeat before going dark
      sendbattery();

      digitalWrite(LIGHTS, HIGH );
      attachInterrupt(0, TSC_Count, RISING);
      Timer1.attachInterrupt(TSC_Callback); 

      delay(1000);

      //send a heartbeat before animating
      sendbattery();

      digitalWrite(LIGHTS,LOW );
      if (g_array[0]+g_array[1]+g_array[2] <20){
        fade=7;
      }
      else{
        color = readcolor();
      }
      //start at top of disc and work around
      for(int i=strip.numPixels()+1; i>-1; i--) {
        strip.setPixelColor((i +xy_angle+8)%16,Wheel(color)); // Set new pixel 'on'
        strip.showCompileTime<ClockPin, DataPin>();              // Refresh LED states
        delay(50);
      }

      z_latch = 1;
      currentmode=0;
    }
  }
  if ((intsource >> ADXL345_INT_SINGLE_TAP_BIT) & 1)  {
  }
  if ((intsource >> ADXL345_INT_INACTIVITY_BIT) & 1)  {
    // currentmode=0;
    // z_latch = 1;
  }

  //normal serial read
  while(Serial.available()){
    currentmode = 0;
    //watch for commands
    switch (Serial.peek()){

    case SET_COLOR:
    case SET_SPAN:
    case SET_FADE_BRIGHTNESS:
    case SET_RAINBOW:
      serialbufferpointer=0;
      break;
    }

    //load a character
    serialbuffer[serialbufferpointer] = Serial.read(); 

    //if done, process
    if(serialbufferpointer == 2){ 
      switch (serialbuffer[0]){
      case SET_COLOR:
        {
          int tempcolor = (serialbuffer[1] << 6) | (serialbuffer[2] >> 1);
          if (tempcolor > 385){
            last_set_color=tempcolor-386;
          }
          else{
            color = tempcolor;
            z_latch_color = tempcolor;//copy into latch buffer incase a new color comes in while gestureing
            last_set_color = tempcolor;
            Serial.write(SET_COLOR);
            tempcolor = tempcolor +386;
            Serial.write((tempcolor >> 6) & 0xFE);
            Serial.write(tempcolor << 1);
          }
          break;
        }
      case SET_SPAN:
        {
          int tempspan=( serialbuffer[1] << 6) | (serialbuffer[2] >> 1);
          if (tempspan > 511 ){
            last_set_span=tempspan-512;
          }
          else {
            span=tempspan;
            z_latch_span=span;  //copy into latch buffer incase a new color comes in while gestureing
            last_set_span = span;
            tempspan = tempspan +512;
            Serial.write(SET_SPAN);
            Serial.write((tempspan >> 6) & 0xFE);
            Serial.write(tempspan << 1);
          }
          break;
        }
      case SET_FADE_BRIGHTNESS: //never setting brightness from disc,always reply with std data for confirmation
        if (serialbuffer[1] > 7 ){
          last_set_fade=serialbuffer[1]-8;

        } 
        else{
          fade = serialbuffer[1];
          brightness = serialbuffer[2]-127;
          last_set_fade=fade;
          if (fade == 0){
            currentmode=0;
          }
          Serial.write(SET_FADE_BRIGHTNESS);
          Serial.write(fade+8);
          Serial.write(brightness+127);//notused padding
        }
        break;
      default:
        serialbuffer[0] = 0xff;

      case SET_RAINBOW: //never confirming rainbow
        rainbowoffset = (serialbuffer[1] << 6) | (serialbuffer[2] >> 1);
        break;
      }
    }
    serialbufferpointer++;

    if (serialbufferpointer>2){
      serialbufferpointer=0;
    }
  }

  //serial write
  if(millis()-lastupdate >50){

    if (last_set_color != color){
      Serial.write(SET_COLOR);
      Serial.write((color >> 6) & 0xFE); //transmit higher bits
      Serial.write(color << 1); //transmit lower bits
    }

    if (last_set_span != span){
      Serial.write(SET_SPAN);
      Serial.write((span >> 6) & 0xFE); //transmit hfcoloigher bits
      Serial.write(span << 1); //transmit lower bits
    }

    if (last_set_fade != fade){
      Serial.write(SET_FADE_BRIGHTNESS);
      Serial.write(fade); //data small enough (0-7)it wont collide
      Serial.write(brightness+127); //not used, but needed for padding
    }

    lastupdate=millis();
  }


  int x_absolute,y_absolute,z_absolute;
  adxl.readAccel(&x_absolute, &y_absolute, &z_absolute); 

  //calculate X 
  int x_relative = x_absolute - previous_x_absolute;
  x_filtered = x_filtered * .95 +x_relative; 

  //calculate Y 
  int y_relative= y_absolute - previous_y_absolute;
  y_filtered = y_filtered * .95 + y_relative; 

  //calculate Z 
  int z_relative = z_absolute - previous_z_absolute;
  z_filtered = z_filtered *.9 + z_relative ; 

  //calculate XY magnitude and angle and convert to LED range 0 -15
  xy_magnitude = (int)sqrt((long)x_relative*x_relative+(long)y_relative*y_relative);
  xy_filtered_magnitude = (int)sqrt((long)y_filtered*y_filtered+(long)x_filtered*x_filtered);
  xy_angle =  (int)(floor(((atan2(y_absolute,x_absolute) *2.54)+8.5))) % 16;
  xy_filtered_angle =  (int)(floor(((atan2(y_filtered,x_filtered) *2.54)+8.5))) % 16;
  if ((z_latch !=3 && z_latch !=4 )) {
    if  (xy_angle == previous_xy_angle) { //angle must go in the same direction
      if (xy_filtered_magnitude > previous_xy_filtered_magnitude && xy_filtered_magnitude > 150){  //filtered magnitude must be strong and rising 
        idle_timer=millis();
        angle = xy_filtered_angle;
        if (fade < 7){   //swipe
          magnitude = map(xy_filtered_magnitude,150,225,1,4);
          currentmode  = 1;  //normal swipe
        }
        else{ //pacman open
          span =0;
          fade =0;
          instantspan = 0;
          brightness = 127;
          color=192;
          for(magnitude=0; magnitude<10; magnitude++ ){
            updatearray();
            strip.showCompileTime<ClockPin, DataPin>(); 
            delay(50);
          }
          
          z_latch = 0;
          currentmode=0;
        }
        swipetime = millis();
      }
    }

    //enter mode with force of 40 from swipe mode, or 10 once in the mode
    if ( (xy_filtered_magnitude > 10 && currentmode == 2 ) || (xy_filtered_magnitude > 100 &&  currentmode ==1 && (swipetime + 100 < millis()) ) ){ 
      //spin mode
      currentmode  = 2;
      angle = xy_filtered_angle;
      magnitude =map(xy_filtered_magnitude,20,100,1,3);
      idle_timer=millis();
    }
  }

  //detect high z motion
  if(abs(z_filtered) > 100) {  //enter mode 0 
    if(currentmode != 0) {
      z_latch_time_cooldown = millis();
      currentmode = 0;
      z_latch = 0;
    }
    if (z_latch == 1){  //enable latched mode
      z_latch_time_cooldown = millis();
      z_latch_angle = xy_angle;
      z_latch = 2;
      idle_timer=millis();
    }
    else if (z_latch == 3 ||  z_latch == 4){  //disable latched mode
      z_latch_time_cooldown = millis();
      z_latch =0;
      tap_time = millis();
    }
  }
  //detect low z motion 
  else if(abs(z_filtered) <10 &&  z_latch_time_cooldown + 500 < millis()) {  
    if (z_latch == 0){
      z_latch = 1;
    }
    if (z_latch == 2){
      rotation_status =0;
      z_latch = 3;
      tap_time = millis();
    }
  }

  if (currentmode == 0){
    magnitude = 9;

    //z is 128 at horizontal (over 96) and below 32 (near 0 at verticle)
    //use absolute x and y and then add in filter x and y for exaggeration of movement

    //addin relative movenet proprtioanlly to angle
    int  z_absolute_ratio= abs(constrain(abs(z_absolute),0,128)-128) >>4;

    //deadzone to help remove jitter
    if (abs(x_absolute-previous_x_absolute) >1||  abs(y_absolute-previous_y_absolute) >1){
      angle = (int)(floor(((atan2(y_absolute+y_filtered*z_absolute_ratio,x_absolute+x_filtered*z_absolute_ratio) *2.54)+8.5))) % 16;
    }

    //color rotate code is 3, span rotate is 4
    if(z_latch ==3 || z_latch ==4 ){

      //cancel latch on z tilt over 100, but allow z accelerations
      if (abs(z_absolute) > (abs(z_filtered )+1)*100){
        z_latch =0;

      }
      int z_latch_angle_difference;

      //find shortest path around circle to get between the starting and ending point
      if((( xy_angle - z_latch_angle + 16) % 16) < 8){
        z_latch_angle_difference= z_latch_angle - xy_angle;
        if (z_latch_angle_difference < -8){
          z_latch_angle_difference = z_latch_angle_difference +16;
        } 
        else if (z_latch_angle_difference > 8){
          z_latch_angle_difference =z_latch_angle_difference -16;
        }
      }
      else {
        z_latch_angle_difference= xy_angle - z_latch_angle;

        if (z_latch_angle_difference < -8){
          z_latch_angle_difference = z_latch_angle_difference +16;
        } 
        else if (z_latch_angle_difference > 8){
          z_latch_angle_difference =z_latch_angle_difference -16;
        }
        z_latch_angle_difference=  z_latch_angle_difference*-1;
      }

      // if we did a rotation swap modes (color and span)
      if(abs(z_latch_angle_difference) == 8){
        z_latch_span = span;  //init span rotate
        z_latch_angle = (z_latch_angle + 8) %16;
        z_latch =4;
      }

      if (z_latch ==3){ //color change code
        if(rotation_status ==0 && z_latch_angle_difference == 0){  //init color rotate
          z_latch_color = color;
          overlaytimer = 30;
          rotation_status =1;  
        }
        if(rotation_status == 1){
          color =z_latch_color + z_latch_angle_difference*16;
          color = (color + 384) % 384;
          if(abs( z_latch_angle_difference) == 4){  //finish color rotate
            overlaytimer = 30;
            rotation_status =0;  
          }
        }
      }
      else if  (z_latch ==4){ //span change code

        span =  z_latch_span + abs(z_latch_angle_difference)*32;

        //latch span to increments of 100 
        if ( (z_latch_span -(z_latch_span % 128))  > span){
          span = z_latch_span -(z_latch_span % 128);
        }
        else if ( (z_latch_span -(z_latch_span % 128) +128)  < span){
          span =z_latch_span -(z_latch_span % 128) +128;
        }

        //wrap span to circle
        span =(span+512) % 512;

        if(abs( z_latch_angle_difference) == 4){  //finish span rotate
          z_latch = 3;
          z_latch_angle = (z_latch_angle + 8) %16;
        }
      }
    }
  }


  //updates the strip
  //i must be global for rainbow colors
  //angle is 0 to 15  22.5 degrees each
  //magnitude is 0 to 9  0 is blank, 9 is full

  updatearray();

  //overlay code
  if (z_latch ==3){
    if (overlaytimer > 0){
      overlaytimer--;
      idle_timer=millis();
    } 
    else{
      strip.setPixelColor(z_latch_angle ,0);
      strip.setPixelColor((z_latch_angle + 8) % 16,0);
    }
  }
  else  if (z_latch ==4 ){
    strip.setPixelColor(z_latch_angle ,Wheel(384));
    strip.setPixelColor((z_latch_angle + 8) % 16,Wheel(384));
  }

  strip.showCompileTime<ClockPin, DataPin>(); 

  //store values for next cycle use
  previous_x_absolute = x_absolute;
  previous_y_absolute = y_absolute;
  previous_z_absolute = z_absolute;
  previous_xy_magnitude = xy_magnitude;
  previous_xy_filtered_magnitude = xy_filtered_magnitude;
  previous_xy_angle = xy_angle;
  previous_xy_filtered_angle = xy_filtered_angle;
}

void updatearray(){
  for(int imag=0; imag<= 9; imag++) {
    if(imag <= magnitude && imag > 0){
      instantspan = map(imag,1,magnitude,0,SpanWheel(span));
      i = (angle+imag-1+16) % 16;
      strip.setPixelColor(i ,Wheel(color));
      i = (angle-imag+1+16) % 16;
      strip.setPixelColor(i ,Wheel(color));
    }
    else{
      i = (angle+imag-1+16) % 16;
      strip.setPixelColor(i ,0);
      i = (angle-imag+1+16) % 16;
      strip.setPixelColor(i ,0);
    }
  }
}

void sendbattery(){
  Serial.write(BATTERY_LEVEL);
  Serial.write((disc_voltage >> 6) & 0xFE);
  Serial.write(disc_voltage << 1);
  batteryupdate=millis();
}

int SpanWheel(int SpanWheelPos){
  int tempspan;
  //map span of 0 128 256 384 to span circle of 0 128 0 -128 
  if (SpanWheelPos > 127 && SpanWheelPos < 384){
    tempspan = 256-SpanWheelPos;
  } 
  else if (SpanWheelPos >= 384){
    tempspan = -512+SpanWheelPos;
  }
  else{
    tempspan = SpanWheelPos;
  }
  return tempspan;
}

int readcolor(){
  float red= float( g_array[0] );
  float green = float(g_array[1] * 1.1412);  //sqrt of 2 due to color cube
  float blue= float(g_array[2] );

  float maxvalue = max(max(red,blue),green);
  float minvalue  = min(min(red,blue),green); 
  float chroma = maxvalue - minvalue;

  float h;
  //this is based on the HSL to RGB equations on wikipedia
  //I added correction factors to compensate for my sensors LEDs etc
  if (maxvalue == red){
    h = ((green - blue)/chroma);  //red correction factor
    if (h > 6){
      h = h -6; 
    }
    h = (h + 0.1) * 1.2; 
  }
  else if (maxvalue == green){
    h = ((green - red)/chroma)+1;   //green correction factor
  }
  else if (maxvalue== blue){
    h = ((red - green)/(chroma)) ;   //blue correction factor
    h =(h + 0.35);   
    if (h < 0){
      h = h * 4 + 4; 
    }
    else{
      h = h * (2 - h) + 4 ;
    }
  }

  //convert 360 degrees to a 383 color wheel number
  h = (h * 60 * 383) / 360;
  while (h > 383){
    h = h - 383;
  }
  while (h < 0){
    h = h + 383;
  }

  return (int)h;
}


uint32_t Wheel(uint16_t WheelPos){
  byte r, g, b;
  if (WheelPos == 385){
    WheelPos = ((int)(i *23.9375) + rainbowoffset + 383) % 384;    //19.25 is 383 (number of colors) divided by 20 (number of LEDs)
  }

  //color span code
  if (WheelPos < 384){
    WheelPos = (WheelPos + instantspan +384) % 384;
  }

  switch(WheelPos / 128)
  {
  case 0:
    r = (127 - WheelPos % 128) ;   //Red down
    g = (WheelPos % 128);      // Green up
    b = 0;                  //blue off
    break; 
  case 1:
    g = (127 - WheelPos % 128);  //green down
    b =( WheelPos % 128) ;      //blue up
    r = 0;                  //red off
    break; 
  case 2:
    b = (127 - WheelPos % 128);  //blue down 
    r = (WheelPos % 128 );      //red up
    g = 0;                  //green off
    break; 
  case 3:
    r = 42;
    g = 42;
    b = 42;
    break; 
  case 4:
    r = 127;
    g = 127;
    b = 127;
    break; 
  }

  r = r*brightness/127;
  g = g*brightness/127;
  b = b*brightness/127;
  return(strip.Color( r >> fade ,g >> fade,b >> fade));
}




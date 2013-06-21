
#include "LPD8806.h"
#include "SPI.h"
#include <Wire.h>
#include <ADXL345.h>
#include <MovingAverage2.h>

#include <MD_TCS230.h>
#include <FreqCount.h>


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


#define LIGHTS 3

#define  S0_OUT  6
#define  S1_OUT  7
#define  S2_OUT  4
#define  S3_OUT  2
MD_TCS230	CS(S2_OUT, S3_OUT, S0_OUT,S1_OUT );


int color = 0;
byte fade = 0;
byte brightness = 127;
byte disc_mode = 0;
unsigned long disc_mode_timer = 0; 
int span = 0;
int instantspan = 0;
int magnitude=8;
int angle=0;


int disc_voltage=1024;
unsigned long last_update=0;
unsigned long battery_update=0;
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

int z_mode_color = 0;  //color at time of first latch
int z_mode_angle = -1; //angle of first latch -1 means hasnt occured yet
unsigned long z_mode_time_cooldown = 0; //time of first latch for input debounce
byte z_mode = 1; //keeps track of latching status
byte rotation_status  =0;  
int z_mode_span;

//overlay
int overlaytimer = 0;  //blinks the LED for modes


unsigned long  tripletap_time=0;
void setup()
{

  CS.begin();
  CS.setSampling(10);

  //black balance calibration data
  static sensorData	DarkCal;
  DarkCal.value[TCS230_RGB_R]=300;
  DarkCal.value[TCS230_RGB_G]=295;
  DarkCal.value[TCS230_RGB_B]=320;
  CS.setDarkCal(&DarkCal);	

  //white balance calibration data
  static sensorData	WhiteCal;
  WhiteCal.value[TCS230_RGB_R]=7420;
  WhiteCal.value[TCS230_RGB_G]=6905;
  WhiteCal.value[TCS230_RGB_B]=7880;
  CS.setWhiteCal(&WhiteCal);

  Serial.begin(115200);
  strip.begin();

  //color sensor lights
  pinMode(LIGHTS, OUTPUT);
  digitalWrite(LIGHTS, LOW);

  //accelerometer init
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
  adxl.setInterrupt( ADXL345_INT_ACTIVITY_BIT, 0);
  adxl.setInterrupt( ADXL345_INT_INACTIVITY_BIT, 0);

  //populate starting values
  adxl.readAccel(&previous_x_absolute, &previous_y_absolute, &previous_z_absolute); 
} 


void loop()
{


  //idle timeout
  if (millis()- idle_timer > 10000){
    z_mode = 1;
    disc_mode=0;
  }

  //battery level check
  //650 = 9.45v
  //used in display update function
  disc_voltage = disc_voltage * .97 + analogRead(0) * .03;

  //send heartbeat to suit every second
  if (millis() - battery_update > 1000){
    Serial.write(BATTERY_LEVEL);
    Serial.write((disc_voltage >> 6) & 0xFE);
    Serial.write(disc_voltage << 1);
    battery_update=millis();
  }

  byte intsource = adxl.getInterruptSource();
  if ((intsource >> ADXL345_INT_DOUBLE_TAP_BIT) & 1)  {
    //if the overlay 
    if (millis() - tripletap_time < 200){
      Serial.write(TRIPLE_TAP);
      for(int i=0; i<strip.numPixels(); i++) {
        strip.setPixelColor(i,0);
      }
      strip.showCompileTime<ClockPin, DataPin>();          
      delay(100);
    }
    else{
      if (disc_mode > 2){
        disc_mode = 0;
      }
      else{
        disc_mode = 3;
        disc_mode_timer = millis();
        z_mode_angle = xy_angle;
      }
    }
  }
  if ((intsource >> ADXL345_INT_SINGLE_TAP_BIT) & 1)  {
  }
  if ((intsource >> ADXL345_INT_INACTIVITY_BIT) & 1)  {
  }

  //read in serial data, limit to 253 bytes per cycle
  byte bytes_read=0;
  while(Serial.available() && bytes_read < 254){
    bytes_read++;

    //watch for data start bytes and adjust 
    switch (Serial.peek()){
    case SET_COLOR:
    case SET_SPAN:
    case SET_FADE_BRIGHTNESS:
    case SET_RAINBOW:
      serialbufferpointer=0;
      break;
    }

    serialbuffer[serialbufferpointer] = Serial.read(); 

    //if buffer has a valid packet process it
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
            z_mode_color = tempcolor;//copy into latch buffer incase a new color comes in while gestureing
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
            z_mode_span=span;  //copy into latch buffer incase a new color comes in while gestureing
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
            disc_mode = 0;
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

  //accelerometer math
  int x_absolute,y_absolute,z_absolute;
  adxl.readAccel(&x_absolute, &y_absolute, &z_absolute); 

  int x_relative = x_absolute - previous_x_absolute;
  x_filtered = x_filtered * .95 +x_relative; 

  int y_relative= y_absolute - previous_y_absolute;
  y_filtered = y_filtered * .95 + y_relative; 

  int z_relative = z_absolute - previous_z_absolute;
  z_filtered = z_filtered *.9 + z_relative ; 

  //calculate XY magnitude and angle and convert to LED range 0 -15
  xy_magnitude = (int)sqrt((long)x_relative*x_relative+(long)y_relative*y_relative);
  xy_filtered_magnitude = (int)sqrt((long)y_filtered*y_filtered+(long)x_filtered*x_filtered);
  xy_angle =  (int)(floor(((atan2(y_absolute,x_absolute) *2.54)+8.5))) % 16;
  xy_filtered_angle =  (int)(floor(((atan2(y_filtered,x_filtered) *2.54)+8.5))) % 16;



  if ((z_mode !=3 && z_mode !=4 )) {
    if  (xy_angle == previous_xy_angle) { //angle must go in the same direction
      if (xy_filtered_magnitude > previous_xy_filtered_magnitude && xy_filtered_magnitude > 150){  //filtered magnitude must be strong and rising 
        idle_timer=millis();
        z_mode = 0;
        if (fade < 7 && disc_mode != 6){ //keep locked in mode 6
          magnitude = map(xy_filtered_magnitude,150,225,1,4);
          disc_mode  = 1;  //normal swipe
          angle = xy_filtered_angle;
        }
        else if (fade == 7 && disc_mode != 6){ //dont reenter
          disc_mode = 6; //pacman open
          z_mode_angle = xy_filtered_angle; //start at hand side
        }
        disc_mode_timer = millis();
      }
    }
  }

  //these two bits of code increment and decrement z_mode during periods of high and low z motion
  //its kind of like tap detection, but is more reliable and gives more control

  //detect high z motion when not in camera modes
  if(abs(z_filtered) > 100 && disc_mode < 3 ) {
    //if in mode 1 or 2, enter mode 0
    if(disc_mode ==1 || disc_mode ==2 ) {
      z_mode_time_cooldown = millis();
      disc_mode = 0;
      z_mode = 0;
    }
    //if already in mode 0
    else if (disc_mode ==0 ){
      //if we are in overlay mode and get high z motion enable overlay mode on next low motion 
      if (z_mode == 1){  
        z_mode_time_cooldown = millis();
        z_mode_angle = xy_angle;
        z_mode = 2;
        idle_timer=millis();
      }
      //if we are in overlay mode and get high z motion disable overlay
      else if (z_mode == 3 ||  z_mode == 4){  
        z_mode_time_cooldown = millis();
        tripletap_time = millis();
        z_mode =0;
      }
    }
  }

  //detect low z motion 
  else if(abs(z_filtered) <10 &&  z_mode_time_cooldown + 200 < millis()) {  
    //enable idle mode from overlay
    if (z_mode == 0){
      z_mode = 1;
    }
    //enable overlay from idle
    else if (z_mode == 2){
      rotation_status =0;
      z_mode = 3;
    }
  }

  if (fade == 7&& disc_mode != 6){
    disc_mode=0; 
    z_mode = 1;
  }

  if ( (xy_filtered_magnitude > 10 && disc_mode == 2 ) || (xy_filtered_magnitude > 100 && disc_mode ==1 && (disc_mode_timer + 100 < millis()) ) ){
    //spin mode
    disc_mode = 2;
    angle = xy_filtered_angle;
    magnitude =map(xy_filtered_magnitude,20,100,1,3);
    idle_timer=millis();
  }


  switch(disc_mode){
  case 0: //normal idle mode, what the gui gets overlayed above. 
    {
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
      if(z_mode ==3 || z_mode ==4 ){

        //cancel latch on z tilt over 100, but allow z accelerations
        if (abs(z_absolute) > (abs(z_filtered )+1)*100){
          z_mode =0;

        }
        int z_mode_angle_difference;

        //find shortest path around circle to get between the starting and ending point
        if((( xy_angle - z_mode_angle + 16) % 16) < 8){
          z_mode_angle_difference= z_mode_angle - xy_angle;
          if (z_mode_angle_difference < -8){
            z_mode_angle_difference = z_mode_angle_difference +16;
          } 
          else if (z_mode_angle_difference > 8){
            z_mode_angle_difference =z_mode_angle_difference -16;
          }
        }
        else {
          z_mode_angle_difference= xy_angle - z_mode_angle;

          if (z_mode_angle_difference < -8){
            z_mode_angle_difference = z_mode_angle_difference +16;
          } 
          else if (z_mode_angle_difference > 8){
            z_mode_angle_difference =z_mode_angle_difference -16;
          }
          z_mode_angle_difference=  z_mode_angle_difference*-1;
        }

        // if we did a rotation swap modes (color and span)
        if(abs(z_mode_angle_difference) == 8){
          z_mode_span = span;  //init span rotate
          z_mode_angle = (z_mode_angle + 8) %16;
          z_mode =4;
        }

        if (z_mode ==3){ //color change code
          if(rotation_status ==0 && z_mode_angle_difference == 0){  //init color rotate
            z_mode_color = color;
            overlaytimer = 30;
            rotation_status =1;  
          }
          if(rotation_status == 1){
            color =z_mode_color + z_mode_angle_difference*16;
            color = (color + 384) % 384;
            if(abs( z_mode_angle_difference) == 4){  //finish color rotate
              overlaytimer = 30;
              rotation_status =0;  
            }
          }
        }
        else if  (z_mode ==4){ //span change code

          span =  z_mode_span + abs(z_mode_angle_difference)*32;

          //latch span to increments of 100 
          if ( (z_mode_span -(z_mode_span % 128))  > span){
            span = z_mode_span -(z_mode_span % 128);
          }
          else if ( (z_mode_span -(z_mode_span % 128) +128)  < span){
            span =z_mode_span -(z_mode_span % 128) +128;
          }

          //wrap span to circle
          span =(span+512) % 512;

          if(abs( z_mode_angle_difference) == 4){  //finish span rotate
            z_mode = 3;
            z_mode_angle = (z_mode_angle + 8) %16;
          }
        }
      }

      updatearray();

      //overlay code
      if (z_mode ==3){
        if (overlaytimer > 0){
          overlaytimer--;
          idle_timer=millis();
        } 
        else{
          strip.setPixelColor(z_mode_angle ,0);
          strip.setPixelColor((z_mode_angle + 8) % 16,0);
        }
      }
      else  if (z_mode ==4 ){
        strip.setPixelColor(z_mode_angle ,Wheel(384));
        strip.setPixelColor((z_mode_angle + 8) % 16,Wheel(384));
      }
    }
    break;
  case 1:  //single swipe mode
  case 2: //spin mode
    updatearray();
    break;
  case 3: //camera opening
    {
      unsigned long temp_time = constrain(millis()-disc_mode_timer,0,1000);
      fade=0;
      span=0;
      instantspan=0;
      z_mode = 1;

      byte temp = map(temp_time,0,1000,0,16);
      for(byte i=0; i<temp; i++) {
        //start at top of disc and work around
        strip.setPixelColor((i+z_mode_angle+8 )%16,Wheel(384)); // Set new pixel 'on'
      }
      if (temp_time == 1000){
        disc_mode++;
        digitalWrite(LIGHTS, HIGH);
        CS.read();
        disc_mode_timer=millis();
      }
    }
    break;
  case 4: //camera on
    {
      if ( CS.available()){
        colorData	rgb;
        sensorData	sd;
        CS.getRaw(&sd);	
        CS.getRGB(&rgb);
        digitalWrite(LIGHTS, LOW);

        //if dark, shut down
        if (sd.value[TCS230_RGB_R]+sd.value[TCS230_RGB_G]+sd.value[TCS230_RGB_B] <200){ 
          fade=7;
          disc_mode=0;
          z_mode = 1;
        }
        else{
          //if color is flooded, ignore
          if(rgb.value[TCS230_RGB_R] != 255 && rgb.value[TCS230_RGB_G] != 255 && rgb.value[TCS230_RGB_B] != 255){
            //if too white, ignore
            if (sd.value[TCS230_RGB_R]+sd.value[TCS230_RGB_G]+sd.value[TCS230_RGB_B] < 12000){
              color = RGBtoHSV(rgb.value[TCS230_RGB_R],rgb.value[TCS230_RGB_G],rgb.value[TCS230_RGB_B]);
              last_set_color=color; //supress sending color
              span=0;
            }
          }
          disc_mode++;
          disc_mode_timer=millis();
        }
      }
    }
    break;
  case 5: //camera closing
    {
      unsigned long temp_time = constrain(millis()-disc_mode_timer,0,1000);
      byte temp = map(temp_time,0,1000,0,16);
      for(int i=temp; i>-1; i--) {
        strip.setPixelColor((i +z_mode_angle+8)%16,Wheel(color)); // Set new pixel 'on'
      }
      if (temp_time == 1000){
        disc_mode = 0;
        z_mode = 1;
        last_set_color = -1; //force sending color at close
      }
    }
    break;
  case 6: //pacman open
    {
      unsigned long temp_time = constrain(millis()-disc_mode_timer,0,500);
      magnitude = map(temp_time,0,500,0,9);
      span = 0;
      fade = 0;
      angle = z_mode_angle;
      instantspan = 0;
      brightness = 127;
      color = 192; //tron color cyan
      last_set_fade = fade; //supress sending fade
      updatearray();
      if (temp_time == 500){
        disc_mode = 0;
        z_mode = 1;
        last_set_fade = 8; //force sending color at close
      }
    }
    break;
  default: //we shouldnt get here ever
    disc_mode = 0;
  }

  strip.showCompileTime<ClockPin, DataPin>(); 

  //serial data write
  if(millis()-last_update >50){
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
    last_update=millis();
  }

  //store values for next cycle use - not all being used
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

  //if the battery is low blank all but 4 LEDs
  if (disc_voltage < 650){
    for(int i=0; i<strip.numPixels(); i++) {
      if (i%4 != 0){
        strip.setPixelColor(i,0); 
      }
    }
  }

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

int RGBtoHSV(byte red,byte green, byte blue){

  byte maxvalue = max(max(red,blue),green);
  byte minvalue  = min(min(red,blue),green); 
  byte chroma = maxvalue - minvalue;
  float h;

  //this is based on the HSL to RGB equations on wikipedia
  //I added correction factors to compensate for my sensors LEDs etc
  if (maxvalue == red){
    h = (((float)(green - blue))/((float)chroma));  
    while (h > 6){
      h = h -6; 
    }
    h = h + 0.1; //offset to 0, aiming for red pantone 185
    if (h < 0){
      h = h  * 1; //exaggerate towards -1, aiming for magenta pantone 238
    }
    else {
      h = h  * 1.25; //exaggerate towards 1, aiming for yellow pantone 102
    }
  }
  else if (maxvalue == green){
    h = (((float)(blue - red))/((float)chroma)); 
    h = h + 0.25; //offset to zero, aiming for green pantone 361
    if (h < 0){
      h = h  * 1.5; //exaggerate towards -1, aiming for yellow pantone 102
    }
    else {
      h = h  * .5; //lessen towards 1, aiming for cyan pantone 306
    }
    h = h  + 2; //recenter on correct color wheel location
  }
  else if (maxvalue == blue){
    h = (((float)(red - green))/((float)chroma));
    h = (h + 0.60);    //offset to 0, aiming for blue pantone 293
    if (h < 0){
      h = h  * 3.333; //exaggerate towards -1, aiming for cyan pantone 306
    }
    else{
      h = h  * 1.3; //exaggerate towards 1, aiming for magenta pantone 238
    }
    h = h  + 4; //recenter on correct color wheel location
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
  return(strip.Color(r >> fade ,g >> fade,b >> fade));
}








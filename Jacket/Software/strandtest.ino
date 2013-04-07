
#include <MovingAverage.h>

#include "LPD8806.h"
#include "SPI.h"
#include "ArduinoNunchuk.h"

#include <Wire.h>

#define spanrange 64 //half range of the span gesture
#define colorrange 32 //half range of the color gesture

// Bump serial buffer to 128 in HardwareSerial.ccp so a full 20x4 frame fits in buffer



//serial coms
//several types of communications were tested, with and without unique start, human readable and non.

//the implementation chosen was done to keep the communications fast and and simple for the limited data we are sending here.
//It is not ideal, and will work you into a corner if you try to extend this to other more complex things.
//The start bytes were carefully chosen to not collide with any characters on the ASCII table on the display and with any data.

//an odd number control command is needed since the strips's RGB values are on a scale of 0-127, which get doubled when sent to the helmet where they need to be
//on a scale of 0-254 to PWM the backlight.(ruling out evens)

//that same data gets mapped from 0-127 to 128-254 for displaying as the visual effects on the screen. (ruling out that section)
//It can also be mapped to 0-15 for custom character effects (ruling out that section too)

//all numerical data (color, span, fade, and brightness) are 0-400.  They are too big for a single byte anyway, so they will be transmitted as two bytes.
//to prevent odds, I double all values before transmission (a single quick shift) so they will not collide.  Same number of bytes sent, no collisions.

//this leaves decimal 16-31 available as unique start bytes

//There are 8 unique odd numbers in that range that would work as a unique start byte.
//since we only have less than that unique commands, we can skip sending a command byte after the start byte, and just use a unique start byte for each command.

//fade and brightness used to be transmitted separately, but both are needed at the exact same time for smooth fades anyway, so they are packed together.

//helmet
#define START_COMMAND 0x11

//disc
#define SET_COLOR 0x11
int last_set_color=0;//set to impossible value to force send on boot


#define SET_SPAN 0x13
int last_set_span=128;//set to impossible value to force send on boot

#define SET_FRAME1 0x15
#define SET_FADE_BRIGHTNESS 0x15
byte last_set_brightness=127;
byte last_set_fade=0;

#define SET_FRAME2 0x17

#define SET_RAINBOW 0x19  //no confirmation needed
#define PING 0x19

boolean flipped= false;

//suit reply
#define CONFIRMED 0x11



//msgeq7 pins
#define msgeq7_reset 3
#define msgeq7_strobe 2
int spectrumValueMax[6];  //keeps track of max values for attack and release
byte spectrumValueMute[6];  //mutes selected channels


//strip bank enable pins
#define strip_1 48
#define strip_2 49
#define strip_3 50
#define strip_4 51

//LPD8806 strip pins
const int dataPin = 53;
const int clockPin = 52;
boolean staticdisplayloaded = false;
//global indexes for strip for effects
byte i = 0;
int rainbowoffset = 383;

//accelerometer values
unsigned int xtilt;
unsigned int ytilt;
//unsigned int ztilt;  not used

//keeps track of how long its been since last pump, and blinks indicator if its been "too" long
unsigned long fist_pump_timer=0;
#define fistpump 2000  //2 second alarm time

unsigned long frame_timer=0;
#define frametime 5000  //2 second alarm time

//displaying gesture data on lcd
boolean pumped=false;
//overlay duration variable
unsigned int overlaytime;
unsigned long overlaytimer=0;
byte overlaystatus=0;
byte  overlayprimer=0;

boolean textmessagemode=false;



//modes
byte effectmode = 0;
byte outputmode = 6;
byte brightness=0;
byte overlaybrightness=0;
byte fade=0;
int color=0;  //the chosen color used for effects  0-383 is mapped to the color wheel  384 is white 385 is rainbow 512 is full white

int instantspan=0; //current span for effects set it to something between zero to span before calling wheel()
int span=128;  //circle 0 128 256 384 512 mapped to 0 128 0 -128 0 
int averagespan; //kept track of for LCD screen effects

//gesture data
int latch_data=0;  //track what angle a span gesture change starts at
byte latch_flag=0;  //keep track of if we are in a gesture or not
unsigned long latch_cool_down; //keep track of time  gesture ended at
#define LATCHTIME 200  //milliseconds to cooldown

//debug FPS calculations
unsigned long fpstime=0;
int fps=0;

//serial buffers
byte serial2buffer[81];
byte serial2bufferpointer = 0;
byte serial2payloadsize=0; 
byte frame1[81] = " CONvergence Merchandise will be at the GPS Trivia contest, which takes place th";
byte frame2[81] = " Saturday, April 6th. We will have the new 2013 Midyear T-shirt for sale (availa";
byte frame=0;
byte serial1buffer[3];
byte serial1bufferpointer = 0;

//these filter the inputs from the buttons
//about two fps
#define BUTTONDELAY 50  //milliseconds it takes for a button press to count
#define DOUBLETAPTIME 500  //milliseconds between double taps for it to count
//detecting doubletaps from the nunchuck
byte zc_doubletap_status = 0;
unsigned long zc_doubletap_time;
boolean cButtonDelayed = false;
boolean zButtonDelayed = false;
boolean cButtonLast = false;
boolean zButtonLast = false;
unsigned long cButtonTimer;
unsigned long zButtonTimer;
unsigned long dpadTimer;
//stick data
byte dpadlast = 0x00; 
byte dpad = 0x00;
//dpad masks
#define DPAD_LEFT B00001000
#define DPAD_RIGHT B0000010
#define DPAD_UP B00000001
#define DPAD_DOWN B00000100
#define DPAD_UP_LEFT B00001001
#define DPAD_DOWN_RIGHT B00000110
#define DPAD_UP_RIGHT B00000011
#define DPAD_DOWN_LEFT B00001100
#define DPAD_DEADZONE B00010000
//makes sure dpad input is processed only once
boolean dirpressed=false;




LPD8806 strip = LPD8806(20, dataPin, clockPin);

ArduinoNunchuk nunchuk = ArduinoNunchuk();
MovingAverage xfilter = MovingAverage();
MovingAverage yfilter = MovingAverage();
//MovingAverage zfilter = MovingAverage();




void setup() {

  strip.begin();
  nunchuk.init();
  Serial.begin(115200);  //debug
  Serial1.begin(115200);  //Wixel
  Serial2.begin(115200);  //BT
  Serial3.begin(115200);  //Helmet 


  pinMode(strip_1,OUTPUT);
  pinMode(strip_2,OUTPUT);
  pinMode(strip_3,OUTPUT);
  pinMode(strip_4,OUTPUT);
  pinMode(clockPin,OUTPUT);
  pinMode(dataPin,OUTPUT);
  //eq
  pinMode(msgeq7_strobe, OUTPUT);
  digitalWrite(msgeq7_strobe, HIGH);
  pinMode(msgeq7_reset, OUTPUT);
  digitalWrite(msgeq7_reset, LOW);
  analogReference(DEFAULT);

  frame1[0]=0;
  frame2[0]=0;
}


void loop() {

  overlayprimer=0;


  readserial();    //service serial ports
  sendserial(); //send serial data

  nunchuk.update();       //read data from nunchuck

  nunchuckparse();  //filter inputs and set D-pad boolean mappings


  if (millis() - fpstime > 1000){
    Serial.println(fps);
    fps=0;

    fpstime=millis();
  }
  fps++;


  //reset variables for monitoring buttons
  if (nunchuk.zButton == 0 && nunchuk.cButton == 0 ){
    dirpressed=false;
    latch_flag=0;   
  }
  //colors
  //effectmode and outputmode settings
  else if (nunchuk.cButton == 1 && nunchuk.zButton == 1){

    //generate one pulse on any input
    if((dpad & 0x0F) != 0x00){
      //opening overlay pulse
      if(fade == 7 || effectmode ==8){
        overlayprimer=4;
      }
      if (dirpressed == false){ 
        //quick one time overlay pulse event to hide transitions
        if (fade!=7 && effectmode !=8){
          overlayprimer=2;
        }
      }
      dirpressed= true;       
    }
    else {
      dirpressed = false;
    }


    //double tap outputmodes
    if(zc_doubletap_status == 3){
      switch (dpad){
      case DPAD_LEFT:
        outputmode =2;
        break;
      case DPAD_RIGHT:
        outputmode =6;
        break;
      case DPAD_UP:
        outputmode =4;
        break;
      case DPAD_DOWN:
        outputmode =0;
        break;
      case DPAD_UP_LEFT:
        outputmode =3;
        break;
      case DPAD_DOWN_RIGHT:
        outputmode =7;
        break;
      case DPAD_UP_RIGHT:
        outputmode =5;
        break;
      case DPAD_DOWN_LEFT:
        outputmode =1;
        break;
      }
    }

    //single tap effect modes
    else { 
      if(overlayprimer!=4){
        switch (dpad){
        case DPAD_LEFT:
          effectmode =2;
          break;
        case DPAD_RIGHT:
          effectmode =6;
          break;
        case DPAD_UP:
          effectmode =4;
          break;
        case DPAD_DOWN:
          effectmode =0;
          break;
        case DPAD_UP_LEFT:
          effectmode =3;
          break;
        case DPAD_DOWN_RIGHT:
          effectmode =7;
          break;
        case DPAD_UP_RIGHT:
          effectmode =5;
          break;
        case DPAD_DOWN_LEFT:
          effectmode =1;
          break;
        }
      }
    }
  }

  if (cButtonDelayed  ){



    switch (dpad){
    case DPAD_LEFT:
      color= 0; //red
      span =0;
      break;
    case DPAD_RIGHT:
      color=192; //cyan
      span =0;
      break;
    case DPAD_UP:
      color= 256; //blue
      span =0;
      break;
    case DPAD_DOWN:
      color= 385;//rainbow - special case
      span =0;
      break;
    case DPAD_UP_LEFT:
      color= 128;//green
      span =0;
      break;
    case DPAD_DOWN_RIGHT:
      color= 320;//purple
      span =0;
      break;
    case DPAD_UP_RIGHT:
      color= 64;//yellow
      span =0;
      break;
    case DPAD_DOWN_LEFT:
      color= 384;//white - special case
      span =0;
      break;
    default:
      if (color < 384){  
        //color gesture
        color = gesture(color,colorrange);

        //wrap color to circle
        color =(color+384) % 384;
      }
      else{
        //unused gesture for special color modes so ascii animation still works
        gesture(color,colorrange);
      }
    }
  }

  //basic setttings and span gestures
  if (zButtonDelayed){

    switch (dpad){
    case DPAD_LEFT:

      break;
    case DPAD_RIGHT:

      break;
    case DPAD_UP:
      textmessagemode=true;
      break;
    case DPAD_DOWN:
      textmessagemode=false;
      break;
    case DPAD_UP_LEFT:
      overlayprimer = 3;
      break;
    case DPAD_DOWN_RIGHT:
      if (fade < 7 && dirpressed==false ) fade++; 
      break;
    case DPAD_UP_RIGHT:
      if (fade > 0 && dirpressed==false ) fade--; 
      break;
    case DPAD_DOWN_LEFT:
      overlayprimer = 1;
      break;
    }

    //generate one change
    if((dpad & 0x0F) != 0x00){
      dirpressed = true;
    }
    else {
      dirpressed = false;

      //span gesture
      span = gesture(span,spanrange);

      //wrap span to circle
      span =(span+512) % 512;

    }
  }



  //code to roll the rainbow left and right
  if (color == 385){
    //25 experimentally chosen to set max speed for rainbow 
    int offsetxtilt = map(xtilt, 0, 254, -25, 25); 
    rainbowoffset = rainbowoffset + offsetxtilt;
    if (rainbowoffset > 383*2){
      rainbowoffset =rainbowoffset - 383; 
    }
    else if (rainbowoffset < 0){
      rainbowoffset =rainbowoffset + 383; 
    }
  }

  //EQ data, always read in even ifnot being used so its ready to go for quick mode switches
  int spectrumValueMaxAll = 0;
  int spectrumValueMin[7];
  int spectrumValue[7]; // to hold a2d values
  digitalWrite(msgeq7_reset, HIGH);
  delay(5);
  digitalWrite(msgeq7_reset, LOW);

  //read data from the EQ, fill array sparcely to fill in later
  for (byte i = 0; i < 7; i++)
  {
    digitalWrite(msgeq7_strobe, LOW);
    delayMicroseconds(40); // to allow the output to settle
    spectrumValue[i] = analogRead(0);
    digitalWrite(msgeq7_strobe, HIGH);
    //  Serial.print(spectrumValue[i]);
    // Serial.print(" ");
  }
  // Serial.println(" ");
  //combine highest two bands and normalize 
  if (spectrumValue[6]  > 90){
    spectrumValue[5] = spectrumValue[5] + spectrumValue[6] - 90;
  }

  for (byte i = 0; i < 6; i++)
  {
    //90 is the input level for mute
    //16 ticks is the max time that can build up
    if(spectrumValue[i] > 90){
      if (spectrumValueMute[i] <10 ){
        spectrumValueMute[i]++;
      }
    }
    else{
      if (spectrumValueMute[i] > 0){
        spectrumValueMute[i]--;
      }
    }
    if (spectrumValueMute[i] < 3){
      spectrumValue[i] = 70;
    }

    spectrumValueMax[i] = max(max(spectrumValueMax[i] * .99 ,spectrumValue[i]),120);
    spectrumValueMin[i] = max( spectrumValueMax[i]*.75 ,90);
    spectrumValueMaxAll = max(spectrumValueMaxAll,spectrumValueMax[i]);
    spectrumValue[i]= constrain(spectrumValue[i],spectrumValueMin[i],spectrumValueMax[i]);
  } 



  //generate effects array based on mode
  if(effectmode == 0){
    averagespan =0;
    for(int i=0; i<6; i++)   {
      brightness = map(spectrumValue[i],spectrumValueMin[i] ,spectrumValueMax[i],0,127); 
      if (brightness > 64){
        instantspan =  map(brightness,64,127,0,SpanWheel(span));
        averagespan = averagespan + instantspan;
      }
      if(i==5){
        strip.setPixelColor(9,  Wheel(color));
        strip.setPixelColor(10,  Wheel(color));
      }
      else{
        strip.setPixelColor(2*i,  Wheel(color));
        strip.setPixelColor(19-2*i,  Wheel(color));
      }
      if (i<4){
        brightness = map((spectrumValue[i+1] + spectrumValue[i]) >> 1,(spectrumValueMin[(i)] + spectrumValueMin[i+1]) >> 1 ,(spectrumValueMax[(i)] + spectrumValueMax[(i)+1]) >> 1,0,127); 
        if (brightness > 64){
          instantspan =  map(brightness,64,127,0,SpanWheel(span));
          averagespan = averagespan + instantspan;

        }
        strip.setPixelColor(19-(2*i+1),  Wheel(color));
        strip.setPixelColor(2*i+1,  Wheel(color));
      }
    }
  }
  else if(effectmode == 1){
    brightness = map(spectrumValue[0]*.3,spectrumValueMin[0]*.3,spectrumValueMax[0]*.3,0,127); 
    if (brightness > 64){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip.setPixelColor(0,  Wheel(color));
    brightness = map(spectrumValue[0]*.6,spectrumValueMin[0]*.6,spectrumValueMax[0]*.6,0,127); 
    if (brightness > 64){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip.setPixelColor(1,  Wheel(color));
    for(int i=0; i<5; i++)   {
      brightness = map(spectrumValue[i],spectrumValueMin[i] ,spectrumValueMax[i],0,127); 
      if (brightness > 64){
        instantspan =  map(brightness,64,127,0,SpanWheel(span));
        averagespan = averagespan + instantspan;
      }
      strip.setPixelColor(i*3+2,  Wheel(color));
      brightness = map(spectrumValue[i+1] * .3 + spectrumValue[i] * .6,spectrumValueMin[i] * .6+ spectrumValueMin[i+1] * .3 ,spectrumValueMax[i] *.6 + spectrumValueMax[i+1] * .3,0,127); 
      if (brightness > 64){
        instantspan =  map(brightness,64,127,0,SpanWheel(span));
        averagespan = averagespan + instantspan;
      }
      strip.setPixelColor(i*3+3,  Wheel(color));
      brightness = map(spectrumValue[i+1] * .6 + spectrumValue[i] * .3,spectrumValueMin[i] * .3+ spectrumValueMin[i+1] * .6 ,spectrumValueMax[i] *.3 + spectrumValueMax[i+1] * .6,0,127); 
      if (brightness > 64){
        instantspan =  map(brightness,64,127,0,SpanWheel(span));
        averagespan = averagespan + instantspan;
      }
      strip.setPixelColor(i*3+4,  Wheel(color));
    }
    brightness = map(spectrumValue[5],spectrumValueMin[5] ,spectrumValueMax[5],0,127); 
    if (brightness > 64){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip.setPixelColor(17,  Wheel(color));
    brightness = map(spectrumValue[5]*.6,spectrumValueMin[5]*.6,spectrumValueMax[5]*.6,0,127); 
    if (brightness > 64){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip.setPixelColor(18,  Wheel(color));
    brightness = map(spectrumValue[5]*.3,spectrumValueMin[5]*.3,spectrumValueMax[5]*.3,0,127); 
    if (brightness > 64){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip.setPixelColor(19,  Wheel(color));
  }

  else if (effectmode == 2){
    brightness = map(ytilt, 0, 254,0, 127);
    instantspan =  map(brightness,0,127,0,SpanWheel(span));
    for( i=0; i<strip.numPixels(); i++)     {
      strip.setPixelColor(i,  Wheel(color));
    }
  }
  else if (effectmode == 3){
    brightness=127;
    byte tempytilt = map(ytilt, 0, 254,0, 20);
    instantspan =  map(tempytilt,0,20,0,SpanWheel(span));
    for( i=0; i<tempytilt; i++)   {
      strip.setPixelColor(i,  Wheel(color));
    }
    for(int i=tempytilt; i<strip.numPixels(); i++) strip.setPixelColor(i, 0);
  }
  else if (effectmode == 4){
    brightness=127;
    byte tempytilt = map(ytilt, 0, 254,0, 40);
    instantspan =  map(tempytilt,0,40,0,SpanWheel(span));
    if (tempytilt < 21){
      for( i=0; i<tempytilt; i++)  strip.setPixelColor(i, Wheel(color));
      for(int i=tempytilt; i<strip.numPixels(); i++) strip.setPixelColor(i, 0);
    }
    else{
      tempytilt = tempytilt -21;
      for( i=20; i>tempytilt; i--)  strip.setPixelColor(i,Wheel(color));
      for(int i=tempytilt; i>-1; i--) strip.setPixelColor(i, 0);
    }
  }
  else if (effectmode ==5){
    brightness=127;
    for( i=0; i<strip.numPixels(); i++) strip.setPixelColor(i, 0);
    byte tempytilt = map(ytilt, 0,254,0, 21);
    instantspan =  map(tempytilt,0,21,0,SpanWheel(span));
    if (tempytilt < 21 and tempytilt >0)  strip.setPixelColor(tempytilt-1, Wheel(color));

  }
  else if (effectmode == 6){
    brightness=127;
    for( i=0; i<strip.numPixels(); i++) strip.setPixelColor(i, Wheel(color));
    byte tempytilt = map(ytilt, 0, 254,0, 21);
    instantspan =  map(tempytilt,0,21,0,SpanWheel(span));
    if (tempytilt < 21 and tempytilt >0)  strip.setPixelColor(tempytilt-1, 0);
  }
  else if (effectmode == 7){


  } 
  else if (effectmode == 8){
    brightness=127;
    for( i=0; i<strip.numPixels(); i++)     {
      instantspan =  map(i,0,19,0,SpanWheel(span));
      strip.setPixelColor(i,  Wheel(color));
    }
    //for display on screen only
    //brightness = map(ytilt, 0, 254,0, 127);
    //instantspan =  map(brightness,0,127,0,SpanWheel(span));
  }


  if (overlayprimer!=0){
    //fadeout
    if(overlayprimer == 1){
      if(nunchuk.accelX > 1000||nunchuk.accelY > 1000 ||nunchuk.accelZ > 1000){
        overlaytimer =millis();
        overlaytime=500;
        fade = 7;
        effectmode = 8;
        overlaystatus=overlayprimer;
      }
    }//fadeduring
    else if(overlayprimer == 2){
      overlaytimer =millis();
      overlaytime=100;
      overlaystatus=overlayprimer;
    }//fade to idle
    else if(overlayprimer == 3){
      if(nunchuk.accelX > 1000||nunchuk.accelY > 1000 ||nunchuk.accelZ > 1000){
        overlaytimer =millis();
        overlaytime=500;
        effectmode=8;
        fade=0;
        overlaystatus=overlayprimer;
      }
    }//fade to on
    else if(overlayprimer == 4){
      if(nunchuk.accelX > 1000||nunchuk.accelY > 1000 ||nunchuk.accelZ > 1000){
        overlaytimer =millis();
        overlaytime=500;
        fade = 0;
        effectmode=9;
        overlaystatus=overlayprimer;
      }
    }
  }

  if(overlaystatus!=0){
    //actually overlay the pixel array
    unsigned long int currenttime = millis();
    if(currenttime<overlaytimer+overlaytime){
      //during overlay
      byte tempfade = fade;
      byte tempbrightness = brightness;
      fade=0;

      //overlay fade equation, change later to make more logorithmic
      overlaybrightness=map(currenttime,overlaytimer,overlaytimer+overlaytime,127,0);

      brightness=overlaybrightness;
      instantspan=0;

      unsigned long  tempcolor =Wheel(color);
      byte r = (tempcolor>> 8) ;
      byte g  = (tempcolor>> 16);
      byte b = (tempcolor>>0);
      instantspan=SpanWheel(span);
      tempcolor =Wheel(color);
      r =r| (tempcolor>> 8);
      g  =g| (tempcolor>> 16);
      b = b|(tempcolor>>0) ;
      for( i=0; i<3*strip.numPixels(); i++)     {
        strip.pixels[i*3+1] = strip.pixels[i*3+1] | r ;
        strip.pixels[i*3]= strip.pixels[i*3] | g;
        strip.pixels[i*3+2] = strip.pixels[i*3+2] |b  ;
      }
      fade=tempfade;
      brightness=tempbrightness;
    }
    else{
      overlaystatus=0;
    }
  }

  //output to the strips
  switch (outputmode){
  case 0: //down
    output(B00000000);
    break;
  case 1: //down left
    output(B00000011);
    break; 
  case 2:  //left
    output(B00001001);
    break;
  case 3: //up left
    output(B00001010);
    break;
  case 4: //up 
    output(B00001111);
    break;
  case 5: //up right
    output(B00001100);
    break;
  case 6: //right
    output(B00000110);
    break;
  case 7: //down right
    output(B00000101);
    break;
  }


  //service LCD display, do this after overlay code has ran so it has all the data to work with
  updatedisplay();    

}

int gesture(int inputvalue, int itemrange){
  //inputvalue rotate code
  int currentvalue;
  if (effectmode == 0 || effectmode ==1|| effectmode ==8){
    currentvalue = xtilt;
  }
  else{
    currentvalue = ytilt;
  }
  currentvalue = map(currentvalue, 0, 254,-(itemrange*2), itemrange*2);

  //save the initial data from when the gesture starts
  if (latch_flag==0){
    if (currentvalue < -itemrange ){  //init cw inputvalue rotate
      latch_data = inputvalue;
      latch_flag=1;
    }
    else if (currentvalue > itemrange ){  //init ccw inputvalue rotate
      latch_data = inputvalue;
      latch_flag=2;
    }
  }
  else if (latch_flag==1){// cw inputvalue rotate
    if (currentvalue < -itemrange){ //init condition
      inputvalue = latch_data;
    } 
    else if (currentvalue > itemrange){ //finish condition
      inputvalue = latch_data+itemrange*2;
      latch_flag=4;
      latch_cool_down = millis();
    } 
    else if (currentvalue < itemrange && currentvalue > -itemrange) { //transition
      inputvalue=latch_data+currentvalue+itemrange;
    }
    //latch inputvalue to increments of (itemrange*2) 
    if ( (latch_data -(latch_data % (itemrange*2)) +(itemrange*2))  < inputvalue){
      inputvalue =latch_data -(latch_data % (itemrange*2)) +(itemrange*2);
    }

  }
  else if (latch_flag==2){ //ccw inputvalue rotate
    if (currentvalue > itemrange){ //init condition
      inputvalue = latch_data;
    } 
    else if (currentvalue < -itemrange){ //finish condition
      inputvalue = latch_data-itemrange*2;
      latch_flag=5;
      latch_cool_down = millis();
    } 
    else if (currentvalue > -itemrange && currentvalue < itemrange) { //transition
      inputvalue=latch_data+currentvalue-itemrange;
    }
    //latch inputvalue to increments of (itemrange*2) 
    if ( (latch_data -(latch_data % itemrange*2))  < inputvalue){
      inputvalue = latch_data -(latch_data % itemrange*2);
    }
  }
  else if (latch_flag > 3){ //rearm after cooldown of 200msec has passed
    if (latch_cool_down + LATCHTIME < millis()){
      if (latch_flag==4){
        if (currentvalue < -itemrange ){ 
          latch_data = inputvalue;
          latch_flag=1;
        }
      }
      else if (latch_flag==5){
        if (currentvalue > itemrange ){  
          latch_data = inputvalue;     
          latch_flag=2;
        }
      }
    }
  }
  return inputvalue;
}


void output(byte w){
  if (bitRead(w,0)) digitalWrite(strip_1,LOW);
  if (bitRead(w,1)) digitalWrite(strip_2,LOW);
  if (bitRead(w,2)) digitalWrite(strip_3,LOW);
  if (bitRead(w,3)) digitalWrite(strip_4,LOW);

  if (w != 0x00){
    strip.showCompileTime<clockPin, dataPin>();
    digitalWrite(strip_1,HIGH);
    digitalWrite(strip_2,HIGH);
    digitalWrite(strip_3,HIGH);
    digitalWrite(strip_4,HIGH);
  }

  if (!bitRead(w,0)) digitalWrite(strip_1,LOW);
  if (!bitRead(w,1)) digitalWrite(strip_2,LOW);
  if (!bitRead(w,2)) digitalWrite(strip_3,LOW);
  if (!bitRead(w,3)) digitalWrite(strip_4,LOW);

  if (w != 0x0F){ 
    if (effectmode == 0){
      strip.showCompileTimeFold<clockPin, dataPin>();
    }
    else{
      strip.showCompileTimeFlip<clockPin, dataPin>();
    }
    digitalWrite(strip_1,HIGH);
    digitalWrite(strip_2,HIGH);
    digitalWrite(strip_3,HIGH);
    digitalWrite(strip_4,HIGH); 
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

uint32_t Wheel(uint16_t WheelPos){
  byte r, g, b;

  //rainbow code
  if (WheelPos == 385){
    WheelPos = ((int)(i * 19.15) + rainbowoffset) % 384;    //19.25 is 383 (number of colors) divided by 20 (number of LEDs)
  }

  //color span code
  if (WheelPos < 384){
    WheelPos = (WheelPos +instantspan +384) % 384;
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


void readserial(){

  //wixel recieving
  while(Serial1.available()){

    switch (Serial1.peek()){
    case SET_COLOR:
    case SET_SPAN:
    case SET_FADE_BRIGHTNESS:
      serial1bufferpointer=0;
      //serial1payloadsize=2;  //all payloads are size 2
      break;
    }

    serial1buffer[serial1bufferpointer] = Serial1.read(); //load a character
    if(serial1bufferpointer == 2){//all payloads are size 2
      //if(serial1bufferpointer == serial1payloadsize){ 
      switch (serial1buffer[0]){

      case SET_COLOR:
        {
          int tempcolor  = (serial1buffer[1] << 6) | (serial1buffer[2] >> 1);
          if (tempcolor > 385){
            last_set_color=tempcolor-386;
          }
          else{
            color = tempcolor;
            latch_data = tempcolor;//copy into latch buffer incase a new color comes in while gestureing
            last_set_color = tempcolor;
            Serial1.write(SET_COLOR);
            tempcolor = tempcolor +386;
            Serial1.write((tempcolor >> 6) & 0xFE);
            Serial1.write(tempcolor << 1);
          }
          break;
        }
      case SET_SPAN:
        {
          int tempspan=( serial1buffer[1] << 6) | (serial1buffer[2] >> 1);
          if (tempspan > 511 ){
            last_set_span=tempspan-512;
          }
          else {
            span=tempspan;
            last_set_span = tempspan;
            latch_data = tempspan;//copy into latch buffer incase a new color comes in while gestureing
            tempspan = tempspan +512;
            Serial1.write(SET_SPAN);
            Serial1.write((tempspan >> 6) & 0xFE);
            Serial1.write(tempspan << 1);
          }
          break;
        }
      case SET_FADE_BRIGHTNESS:
        if (serial1buffer[1] > 7 ){
          last_set_fade=serial1buffer[1]-8;
          last_set_brightness=serial1buffer[2]-127;
        } 
        else{
          fade = serial1buffer[1];
          last_set_fade=serial1buffer[1];
          Serial1.write(SET_FADE_BRIGHTNESS);
          Serial1.write(fade+8);
          Serial1.write(brightness+127);//notused padding
        }
        break;
      }
    }

    serial1bufferpointer++;
    if (serial1bufferpointer>2){
      serial1bufferpointer=0;
    }
  }

  //bluetooth recieving
  while(Serial2.available()){

    switch (Serial2.peek()){
      case PING:
      serial2bufferpointer=0;
      serial2payloadsize=0; 
    case SET_COLOR:
    case SET_SPAN:
      serial2bufferpointer=0;
      serial2payloadsize=2; 
      break;
    case SET_FRAME1:
    case SET_FRAME2:
      serial2bufferpointer=0;
      serial2payloadsize=80; 
      break;
    }

    serial2buffer[serial2bufferpointer] = Serial2.read(); //load a character
    if(serial2bufferpointer == serial2payloadsize){    //all payloads are size 2
      switch (serial2buffer[0]){
        case PING:
         //Serial2.write(CONFIRMED);
      case SET_COLOR:
//Serial2.write(CONFIRMED);
        {
          int tempcolor  = (serial2buffer[1] << 6) | (serial2buffer[2] >> 1);
          color = tempcolor;
          latch_data = tempcolor;//copy into latch buffer incase a new color comes in while gestureing
          last_set_color = tempcolor;
          break;
        }
      case SET_SPAN:
        //Serial2.write(CONFIRMED);
        {
          int tempspan=( serial2buffer[1] << 6) | (serial2buffer[2] >> 1);
          span=tempspan;
          last_set_span = tempspan;
          latch_data = tempspan;//copy into latch buffer incase a new color comes in while gestureing
          tempspan = tempspan +512;
          break;
        }
      case SET_FRAME1:
        //Serial2.write(CONFIRMED);
        {
          memcpy(frame1,serial2buffer,sizeof(serial2buffer));
          frame1[0]=0xff;
          break;
        }
      case SET_FRAME2:
        //Serial2.write(CONFIRMED);
        {
          memcpy(frame2,serial2buffer,sizeof(serial2buffer));
          frame2[0]=0xff;
          break;
        }
      }

    }

    serial2bufferpointer++;

    if (serial2bufferpointer>80){
      serial2bufferpointer=0;
    }
  }
}


void nunchuckparse(){
  byte dpadtemp =0x00;
  if(nunchuk.analogMagnitude > 40){
    if (nunchuk.analogAngle < 10 && nunchuk.analogAngle > -10){
      dpadtemp = DPAD_LEFT;
    }
    else if (nunchuk.analogAngle < 55 && nunchuk.analogAngle > 35){
      dpadtemp =  DPAD_DOWN_LEFT;
    }
    else if (nunchuk.analogAngle < -35 && nunchuk.analogAngle > -55){
      dpadtemp =  DPAD_UP_LEFT;
    }
    else if (nunchuk.analogAngle < -80 && nunchuk.analogAngle > -100){
      dpadtemp =  DPAD_UP;
    }
    else if (nunchuk.analogAngle < 100 && nunchuk.analogAngle > 80){
      dpadtemp =  DPAD_DOWN;
    }
    else if (nunchuk.analogAngle < 145 && nunchuk.analogAngle > 125){
      dpadtemp =  DPAD_DOWN_RIGHT;
    }
    else if (nunchuk.analogAngle < -125 && nunchuk.analogAngle > -145){
      dpadtemp =  DPAD_UP_RIGHT;
    }
    else if (nunchuk.analogAngle < -170 || nunchuk.analogAngle > 170){
      dpadtemp =  DPAD_RIGHT;
    }
    else{
      dpadtemp = DPAD_DEADZONE;
    }
  }

  //dpad noise removal / delay code
  if (dpadtemp == 0x00 || dpadtemp !=dpadlast){
    dpadTimer = millis();
    dpad = 0x00;
  }
  if (millis() - dpadTimer > BUTTONDELAY ){
    dpad = dpadtemp;
  }
  dpadlast = dpadtemp;

  //nunchuck unplugged code
  if(nunchuk.pluggedin == false){
    effectmode = 0;
    outputmode = 6;  
    dpad = 0x00;
  }

  //double tap code
  if ( nunchuk.zButton == 1 &&  nunchuk.cButton == 1){
    if (zc_doubletap_status == 0){
      zc_doubletap_time = millis();
      zc_doubletap_status =1;
    }
    else if (zc_doubletap_status == 2){
      if (millis() - zc_doubletap_time < DOUBLETAPTIME){
        zc_doubletap_status = 3;
      }
    }
  }
  else if ( nunchuk.zButton == 0 && nunchuk.cButton == 0){
    if (millis() - zc_doubletap_time > DOUBLETAPTIME){
      zc_doubletap_status = 0;
    }
    if (zc_doubletap_status == 1){
      zc_doubletap_status =2;
    }
  }



  //z button noise removal / delay code
  if( nunchuk.zButton && zButtonLast == false || nunchuk.cButton){
    zButtonTimer=millis();
  }

  if (nunchuk.zButton && (millis() - zButtonTimer > BUTTONDELAY&& nunchuk.cButton == false)){
    zButtonDelayed = true;
  }
  else{
    zButtonDelayed = false;
  }
  zButtonLast = nunchuk.zButton;

  //c button noise removal / delay code
  if( nunchuk.cButton && cButtonLast == false || nunchuk.zButton){
    cButtonTimer=millis();
  }

  if (nunchuk.cButton && (millis() - cButtonTimer > BUTTONDELAY && nunchuk.zButton == false)){
    cButtonDelayed = true;
  }
  else{
    cButtonDelayed = false;
  }
  cButtonLast = nunchuk.cButton;


  xtilt = xfilter.process(nunchuk.accelX);  //accelerometer based color selection
  xtilt = constrain(xtilt, 350, 650);
  xtilt= map(xtilt, 350, 650,0, 254);

  ytilt = yfilter.process(nunchuk.accelY);
  ytilt = constrain(ytilt, 500, 600);
  ytilt = map(ytilt, 500, 600,0, 254);



  //ztilt = zfilter.process(nunchuk.accelZ);
  //ztilt = constrain(ztilt, 350, 700);
  //ztilt = map(ztilt, 320, 720, 8, 0);

}

void sendserial(){



  if (last_set_color != color){
    Serial1.write(SET_COLOR);
    Serial1.write((color >> 6) & 0xFE); //transmit higher bits
    Serial1.write(lowByte(color) << 1); //transmit lower bits

  }

  if (last_set_span != span ){
    Serial1.write(SET_SPAN);
    Serial1.write((span >> 6) & 0xFE); //transmit higher bits
    Serial1.write(lowByte(span) << 1); //transmit lower bits

  }

  if(overlaystatus == 0 || overlaystatus == 4){
    //set fade before brightness
    if (last_set_fade != fade || last_set_brightness != 127){
      Serial1.write(SET_FADE_BRIGHTNESS);
      Serial1.write(fade); //data small enough (0-7)it wont collide
      Serial1.write(127+127); //encode data to avoid collisions 0-127 moved to 127-254
    }

  }
  else if(overlaystatus == 1){
    if (last_set_brightness != overlaybrightness || last_set_fade != 0){
      Serial1.write(SET_FADE_BRIGHTNESS);
      Serial1.write(0); //data small enough (0-7)it wont collide
      Serial1.write(overlaybrightness+127); //encode data to avoid collisions 0-127 moved to 127-254
    }
  }  
  else if(overlaystatus == 3){
    int tempbrightness = max(overlaybrightness ,127 >> fade);
    if (last_set_brightness != tempbrightness || last_set_fade != 0){
      Serial1.write(SET_FADE_BRIGHTNESS);
      Serial1.write(0); //data small enough (0-7)it wont collide
      Serial1.write(tempbrightness+127); //encode data to avoid collisions 0-127 moved to 127-254
    }
  }


  if  (color == 385){
    Serial1.write(SET_RAINBOW);
    Serial1.write((rainbowoffset >> 6) & 0xFE); //transmit higher bits
    Serial1.write(lowByte(rainbowoffset) << 1); //transmit lower bits
  } 
}

void updatedisplay(){

  //save brightness values for later and set to max val
  byte tempfade = fade;
  byte tempbrightness = brightness;
  if (fade < 7){
    fade = 0;
  }
  brightness=127;

  //extract RGB values from color

  //exaggerate the color change by dividing by less segments than we have, but we have to
  //check the value versus min and max to not go over
  if (effectmode ==0){
    if (SpanWheel(span) < 0){
      instantspan = max(averagespan / 4,SpanWheel(span));
    }
    else{
      instantspan = min(averagespan / 4,SpanWheel(span));
    }
  }

  //if we are not in a dpad mode
  //display pure color or span while changing it
  //so I can see what I am doing
  if(dpad == 0x00 && overlayprimer == 0){
    if ( zButtonDelayed == 1 && cButtonDelayed == 0){
      instantspan=SpanWheel(span);
      fade=0;
    }
    else if ( zButtonDelayed == 0 && cButtonDelayed == 1){
      instantspan=0;
      fade=0;
    }
  }
  if (overlaystatus ==1 ){
    fade=0;
    brightness = overlaybrightness;
  }

  long int tempcolor =Wheel(color);
  byte r = (tempcolor>> 8)& 0x7F;
  byte g  = (tempcolor>> 16)& 0x7F;
  byte b = (tempcolor>>0) & 0x7F;



  //build a data packet to send to the helmet
  Serial3.write(START_COMMAND);


  //output RGB to LCD backlight
  Serial3.write(r<<1);//r
  Serial3.write(g<<1);//g
  Serial3.write(b<<1);//b

  //both frames are loaded, go go animation
  if(textmessagemode){
    //if(frame1[0] > 0 && frame2[0] > 0){
    frame1[0]--;
    frame2[0]--;

    //if fistpump timer has ran out animate based on the clock




    if (millis() - frametime > frame_timer ){ 
      if(((millis() >> 9) & 0x01) == 0x01 ){
        if (flipped == false){
          frame=frame ^ 0x01;
          flipped = true;
          frame_timer = millis();
        }
      } 
    }
    else{
      if(((dpad == DPAD_UP) && zButtonDelayed)){
        if(((millis() >> 6) & 0x01) == 0x01){
          if (flipped == false){
            frame=frame ^ 0x01;
            flipped = true;
            frame_timer = millis();
          }
        } 
        else{
          flipped = false; 
        }
      }
      else{
        if (latch_flag == 4 || latch_flag == 5){
          if (flipped == false){
            frame=frame ^ 0x01;
            flipped = true;
            frame_timer = millis();
          }
        }
        else{
          flipped = false; 
        }
      }
    }


    if (color == 385){
      if(frame == 0){
        Serial3.print(F("-_-_-_-_,------,    _-_-_-_-|   /\\_/\\   -_-_-_-~|__( ^ .^)  _-_-_-_-  \"\"  \"\"    "));
      }
      else if (frame == 1){  
        Serial3.print(F("_-_-_-_-,------,    -_-_-_-_|   /\\_/\\   --_-_-_~|__(^ .^ )  -_-_-_-_ \"\"  \"\"     "));
      }
    }
    else{
      if(frame == 0){
        for (byte i=1; i<81; i++ ) {
          Serial3.write(frame2[i]);
        }
      }
      else if (frame == 1){  
        for (byte i=1; i<81; i++ ) {
          Serial3.write(frame1[i]);
        }
      }
    }
  }

  else{
    Serial3.print(effectmode);
    Serial3.print("  ");
    //update first line of LCD screen
    Serial3.print("R");

    if (r < 10){
      Serial3.print("00");
    } 
    else if (r <100)
      Serial3.print ("0");
    Serial3.print(r,10);


    Serial3.print(" G");
    if (g < 10){
      Serial3.print("00");
    } 
    else if (g <100)
      Serial3.print ("0");
    Serial3.print(g,10);


    Serial3.print(" B");
    if (b < 10){
      Serial3.print("00");
    } 
    else if (b <100)
      Serial3.print ("0");
    Serial3.print(b,10);


    Serial3.print(" ");
    Serial3.print(outputmode);
    Serial3.print(fade);

    //update 2nd  line of LCD screen R values

    for (byte i=0; i<20; i++ ) {
      Serial3.write((strip.pixels[i*3+1] & 0x7F)>>4);
    }

    // update 3rd  line of LCD screen G values
    for (byte i=0; i<20; i++ ) {
      Serial3.write((strip.pixels[i*3]& 0x7F)>>4);
    }

    //update 4th  line of LCD screen B values
    for (byte i=0; i<20; i++ ) {
      Serial3.write((strip.pixels[i*3+2]& 0x7F) >>4 );
    }

  }


  byte gpio=0x00;
  //if not double tapped, determine LED1 and 2 below
  if (zc_doubletap_status !=3){

    //LED1 c button status
    if (nunchuk.cButton){
      //blink out the fade level on LED1, if fade level is greater than zero.
      unsigned long currenttime=  (millis() - cButtonTimer) >> 6;
      if ((((currenttime % 16) < (tempfade << 1)) && ((currenttime & 0x01) == 0x01)) == false) {
        bitSet(gpio,0);
      }
    }

    //LED2 Z button status
    if (nunchuk.zButton){
      //if overlay is primed, blink led2
      if (overlayprimer != 0){
        if((millis()  >> 8) & 0x01 ){
          bitSet(gpio,1);
        }
      }
      //otherwise just turn on led 2
      else{
        bitSet(gpio,1);
      }
    }
  }
  //blink both LED1 and LED2 if doubletapped
  else{
    if((millis()  >> 8) & 0x01 ){ 
      bitSet(gpio,1);
      bitSet(gpio,0);
    }
  }

  //LED3 - dpad status
  if (dpad & 0x0F){
    bitSet(gpio,2);
  }

  //LED4 - motion status

  //idle mode
  if ( effectmode == 8){ 
    //do nothing
  }
  //EQ modes
  else if ( effectmode == 0 || effectmode == 1){  
    //if a button is pressed...
    if (nunchuk.cButton || nunchuk.zButton ){
      //light led 4 based on gesture status
      if(latch_flag == 1 || latch_flag == 2){
        bitSet(gpio,3);
      }
    }
    else{
      //otherwise just light led4 if we tilt extremely to let us know we are in an eq mode
      if(xtilt == 0 || xtilt == 254 ){
        bitSet(gpio,3);
      }     
    }
  } 
  //fist pump modes
  else {  

    //change the pump status based on tilt
    if (ytilt == 0){
      //reset fist pump timer on status change
      if(pumped== true){
        fist_pump_timer= millis();
      }
      pumped=false;
    }
    else if (ytilt == 254 ){
      //reset fist pump timer on statu schange
      if(pumped== false){
        fist_pump_timer= millis();
      }
      pumped=true;
    }

    //if timer has ran out, set off alarm
    if (millis() - fist_pump_timer > fistpump ){
      //fist pumping alarm
      if(((millis() >> 5) & 0x01 )&& fade !=7){
        bitSet(gpio,3);
      }
    }
    //otherwise just display the current fist pumping status
    else{
      if ( pumped ){
        bitSet(gpio,3);
      }
    }
  }

  Serial3.write(gpio);

  //set brightness again
  fade = tempfade;
  brightness = tempbrightness;
}



























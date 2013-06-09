
#include <MovingAverage.h>

#include "LPD8806.h"
#include "SPI.h"
#include "ArduinoNunchuk.h"
#include "EEPROM.h"
#include "avr/eeprom.h"
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
#define SET_COLOR 0x11 //bidirection coms with disc.  Reply back double the value to confirm
int last_set_color= -1 ;//set to impossible value to force send on boot

#define SET_SPAN 0x13  //bidirection coms with disc.  Reply back double the value to confirm
int last_set_span=-1;//set to impossible value to force send on boot

#define SET_FADE_BRIGHTNESS 0x15  //bidirection coms with disc.  Reply back double the value to confirm
byte last_set_brightness=128; //set to impossible value to force sending first time
byte last_set_fade=8;  //set to impossible value to force sending first time

#define SET_RAINBOW 0x19  //sent to disc with rainbow offset value, no data returned
#define TRIPLE_TAP 0x19  //recieved from disc to swap suit modes on triple tap
#define TEXTING_REPLY 0x19  //recieved from phone to ask for stats

#define CONFIRMED 0x11 //suit reply to bluetooth

#define BATTERY_LEVEL 0x1B  //recieved from disc with voltage data
unsigned long heartbeat=0;  //keep track of when the disc last reported in its voltage
unsigned int jacket_voltage=1024;  //voltage monitors
unsigned int disc_voltage=0;

#define SET_FRAME1 0x15  //recieve from phone to set frame 1 data
#define SET_FRAME2 0x17//recieve from phone to set frame 2 data


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

//global indexes for strip for effects
byte i = 0;
int rainbowoffset = 383;

//accelerometer values
unsigned int xtilt;
unsigned int ytilt;

//keeps track of how long its been since last pump, and blinks indicator if its been "too" long

#define fistpump 2000  //2 second alarm time

//displaying gesture data on lcd
boolean beat_completed=false;
//overlay duration variable
unsigned int overlay_duration;
unsigned long overlay_starting_time=0;
byte overlay_mode=0;
byte overlay_primer=0;

//modes
byte effectbuffer_mode = 0; //choose which buffer combination to display
byte active_segment = 1;  //for mode #7, keeps track of which of 5 segments to light
byte effect_mode = 0;
byte output_mode = 0;
byte brightness=0;
byte overlay_brightness=0;
byte fade=0;
int color=0;  //the chosen color used for effects  0-383 is mapped to the color wheel  384 is white 385 is rainbow 512 is full white
byte fade_primer=0;

int instantspan=0; //current span for effects set it to something between zero to span before calling wheel()
int span=128;  //circle 0 128 256 384 512 mapped to 0 128 0 -128 0 
int averagespan; //kept track of for LCD screen effects

//gesture data
int latch_data=0;  //track what angle a span gesture change starts at
byte latch_flag=0;  //keep track of if we are in a gesture or not
unsigned long latch_cool_down; //keep track of time  gesture ended at
#define LATCHTIME 200  //milliseconds to cooldown

//FPS calculations
unsigned long fps_time=0; //keeps track of when the last cycle was
byte fps=0;  //counts up
byte fps_last=0; //saves the value

//serial buffers
byte serial2buffer[81];
byte serial2bufferpointer = 0;
byte serial2payloadsize=0; 
byte serial1payloadsize=0; 
byte frame1[80] = "                                                                               ";
byte frame2[80] = "                                                                               ";

byte frame_mode=0;
boolean frame_toggle_primer=false;
boolean frame_toggle=false;
byte animation_speed = 0;
byte serial1buffer[3];
byte serial1bufferpointer = 0;
boolean diag_toggle_primer = false;
unsigned long display_timer=0;  //go back to default display in 60 seconds
#define DISPLAYTIMEOUT 60000 
unsigned long frame_timer=0;  //switch to alt frame every 5 seconds
#define FRAMETIMEOUT 5000 
unsigned long ring_timer=0; //flash screen to ring for incoming message
#define RINGTIMEOUT 1000 

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
byte dpad_previous = 0x00; 
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
boolean input_processed=false;

LPD8806 strip_buffer_1 = LPD8806(20, dataPin, clockPin);
LPD8806 strip_buffer_2 = LPD8806(20, dataPin, clockPin);

ArduinoNunchuk nunchuk = ArduinoNunchuk();
MovingAverage xfilter = MovingAverage();
MovingAverage yfilter = MovingAverage();

int bpm_period = 1000;  //period in milliseconds
unsigned long bpm_starting_time=0; //keeps track of when the bmp is sampled
byte auto_pump_mode = 0; //0 is off, 1 is on, and higher than that is turbo modes
boolean auto_pump = false;   //autopump mode, do not set this variable, it is automatically set based on auto_pump_mode at the end of a beat
byte auto_pump_multiplier=0;  //autopump mode, do not set this variable, it is automatically set based on auto_pump_mode at the end of a beat

unsigned long beats=0; //total number of beats, only used for stats

unsigned long eeprom_timer = 0;
byte eeprom_mode = 0;
unsigned long eeprom_beats_starting =0;
unsigned long eeprom_time_starting  =0;
unsigned long eeprom_beats_current =0;
unsigned long eeprom_time_current =0;

void setup() {

  strip_buffer_1.begin();
  strip_buffer_2.begin();
  nunchuk.init();
  Serial.begin(115200);  //debug
  Serial1.begin(115200);  //Wixel
  Serial2.begin(115200);  //BT
  Serial3.begin(115200);  //Helmet 

  //strips
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

  //load saved data from eeprom
  eeprom_beats_starting = eeprom_beats_starting | EEPROM.read(0);
  eeprom_beats_starting = eeprom_beats_starting << 8;
  eeprom_beats_starting = eeprom_beats_starting | EEPROM.read(1);
  eeprom_beats_starting = eeprom_beats_starting << 8;
  eeprom_beats_starting = eeprom_beats_starting | EEPROM.read(2);
  eeprom_beats_starting = eeprom_beats_starting << 8;
  eeprom_beats_starting = eeprom_beats_starting | EEPROM.read(3);

  eeprom_time_starting = eeprom_time_starting | EEPROM.read(4);
  eeprom_time_starting = eeprom_time_starting << 8;
  eeprom_time_starting = eeprom_time_starting | EEPROM.read(5);
  eeprom_time_starting = eeprom_time_starting << 8;
  eeprom_time_starting = eeprom_time_starting | EEPROM.read(6);
  eeprom_time_starting = eeprom_time_starting << 8;
  eeprom_time_starting = eeprom_time_starting | EEPROM.read(7);

  //load initial data into text messaging buffers
  sprintf((char*)&frame1[0],"Lifetime Minutes:");
  sprintf((char*)&frame1[20],"%d",eeprom_time_starting);
  sprintf((char*)&frame1[40],"Lifetime Beats:");
  sprintf((char*)&frame1[60],"%d",eeprom_beats_starting);
  memcpy(frame2,frame1,sizeof(frame1));

  //start the timer, it will save to eeprom in one minute from this
  eeprom_timer=millis();
}

void loop() {

  //save data to eeprom every minute, should last 70 days before hitting wear limit
  //I'll manually wear level if needed, there is 4k of eeprom on a mega, and im saving 8 bytes

  //only save one byte each cycle if the eeprom is ready so as not to impact frame rate.
  //we run at about 60 FPS (14ms per frame) so as long as we only save one byte per cycle we are fine
  //it takes about 4ms between writes for the eeprom to get ready again
  if (millis() - eeprom_timer > 60000){
    switch (eeprom_mode){
    case 0:
      //snapshot current data
      eeprom_beats_current = eeprom_beats_starting + beats;
      eeprom_time_current  = eeprom_time_starting + (millis() /60000); //convert millis to minutes
      eeprom_mode++;
      break;
    case 1:
      if (eeprom_is_ready() == true){
        EEPROM.write(0,(byte)(eeprom_beats_current >> 24));
        eeprom_mode++;
      }
      break;
    case 2:
      if (eeprom_is_ready() == true){
        EEPROM.write(1,(byte)(eeprom_beats_current >> 16));
        eeprom_mode++;
      }
      break;
    case 3:
      if (eeprom_is_ready() == true){
        EEPROM.write(2,(byte)(eeprom_beats_current >> 8));
        eeprom_mode++;
      }
      break;
    case 4:
      if (eeprom_is_ready() == true){
        EEPROM.write(3,(byte)eeprom_beats_current);
        eeprom_mode++;
      }
      break;
    case 5:
      if (eeprom_is_ready() == true){
        EEPROM.write(4,(byte)(eeprom_time_current >> 24));
        eeprom_mode++;
      }
      break;
    case 6:
      if (eeprom_is_ready() == true){
        EEPROM.write(5,(byte)(eeprom_time_current >> 16));
        eeprom_mode++;
      }
      break;
    case 7:
      if (eeprom_is_ready() == true){
        EEPROM.write(6, (byte)(eeprom_time_current >> 8));
        eeprom_mode++;
      }
      break;
    case 8:
      if (eeprom_is_ready() == true){
        EEPROM.write(7,(byte)eeprom_time_current);
        eeprom_mode=0;
        eeprom_timer=millis();
      }
      break;
    }

  }
  //from zero to to full brightness the 5v line changes by a few mV due to sagging
  //the lower vref goes the higher the ADC thinks the battery is
  jacket_voltage = jacket_voltage * .97 + analogRead(1) * .03;
  if (jacket_voltage < 720){
    fade=7;
  }
  // Serial.print(disc_voltage); // * 11.11/693 = volts
  // Serial.println(jacket_voltage); // * 15.08/759= volts

  //dont trust disc if it hasnt been heard from in 2 seconds
  if (millis() - heartbeat > 2000){
    last_set_color= -1 ;//set to impossible value to force send on boot
    last_set_span=-1;//set to impossible value to force send on boot
    last_set_brightness=128; //set to impossible value to force sending first time
    last_set_fade=8;  //set to impossible value to force sending first time
    disc_voltage=0;
  }

  if (millis() - fps_time > 1000){
    fps_last = fps;
    fps=0;
    fps_time=millis();
  }
  fps++;

  //reset flags
  overlay_primer=0;
  animation_speed=0;

  readserial();     //read serial data
  sendserial();     //send serial data
  nunchuk.update(); //read data from nunchuck
  nunchuckparse();  //filter inputs and set D-pad boolean mappings

  //reset variables for monitoring buttons
  if (nunchuk.zButton == 0 && nunchuk.cButton == 0 ){
    input_processed=false;
    latch_flag=0;   
  }
  //colors
  //effect_mode and output_mode settings
  else if (nunchuk.cButton == 1 && nunchuk.zButton == 1){
    //generate one pulse on any input
    if((dpad & 0x0F) != 0x00){
      //opening overlay pulse
      if(fade == 7 || effect_mode ==8){
        overlay_primer=4;
      }
      if (input_processed == false){ 
        //quick one time overlay pulse event to hide transitions
        if (fade!=7 && effect_mode !=8 && effect_mode !=7){
          overlay_primer=2;
        }
      }
      input_processed= true;       
    }
    else {
      input_processed = false;
    }

    //double tap output_modes
    if(zc_doubletap_status == 3){
      switch (dpad){
      case DPAD_LEFT:
        output_mode =2;
        break;
      case DPAD_RIGHT:
        output_mode =6;
        break;
      case DPAD_UP:
        output_mode =4;
        break;
      case DPAD_DOWN:
        output_mode =0;
        break;
      case DPAD_UP_LEFT:
        output_mode =3;
        break;
      case DPAD_DOWN_RIGHT:
        output_mode =7;
        break;
      case DPAD_UP_RIGHT:
        output_mode =5;
        break;
      case DPAD_DOWN_LEFT:
        output_mode =1;
        break;
      }
    }

    //single tap effect modes
    else { 
      if(overlay_primer!=4){
        //reset buffer mode on effect changes
        if((dpad & 0x0F) != 0x00){
          effectbuffer_mode=0;
        }
        switch (dpad){
        case DPAD_LEFT:
          effect_mode =2;
          break;
        case DPAD_RIGHT:
          effect_mode =6;
          break;
        case DPAD_UP:
          effect_mode =4;
          break;
        case DPAD_DOWN:
          effect_mode =0;
          break;
        case DPAD_UP_LEFT:
          effect_mode =3;
          break;
        case DPAD_DOWN_RIGHT:
          effect_mode =7;
          break;
        case DPAD_UP_RIGHT:
          effect_mode =5;
          break;
        case DPAD_DOWN_LEFT:
          effect_mode =1;
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
    }
  }


  //basic setttings and span gestures
  if (zButtonDelayed){
    fade_primer =0;
    switch (dpad){
    case DPAD_LEFT: //arm chest switch
      effectbuffer_mode=1;
      break;
    case DPAD_RIGHT: //chest chest seitch
      effectbuffer_mode = 3;
      break;
    case DPAD_UP:
      if( input_processed == false){
        auto_pump_mode=0;
      }
      break;
    case DPAD_DOWN:
      if( input_processed == false){
        auto_pump_mode++;
      }
      break;
    case DPAD_UP_LEFT:
      overlay_primer = 3;
      fade_primer = 2;
      break;
    case DPAD_DOWN_RIGHT:
      if( input_processed == false){
        diag_toggle_primer=true;
      }
      display_timer=millis();  //reset timers
      frame_timer=display_timer;
      break;
    case DPAD_UP_RIGHT:
      animation_speed=5; //speed up while held
      if( input_processed == false){
        if (color != 385 && frame_mode != 2){
          frame_mode=1;  //force to pattern 1 when not in nyan mode or in mode 2
        }
        frame_toggle_primer=true;
        display_timer=millis();  //reset timers
        frame_timer=display_timer;
      }
      break;
    case DPAD_DOWN_LEFT:
      fade_primer = 1;
      overlay_primer = 1;
      break;
    }

    //generate one change
    if((dpad & 0x0F) != 0x00){
      input_processed = true;
    }
    else {
      input_processed = false;
      //span gesture
      span = gesture(span,spanrange);
      //wrap span to circle
      span =(span+512) % 512;
    }
  }


  //adjust fade on combo release
  if (fade_primer == 2&& input_processed==false){
    if (fade > 0  ) fade--; 
    fade_primer =0;
  }
  else if(fade_primer == 1&& input_processed==false){
    if (fade < 7 && input_processed==false ) fade++; 
    fade_primer =0;
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
  if(effect_mode == 0){
    auto_pump_mode=0;

    //buffer modes 3 and 4 are ugly with effect 0 so force switch it
    if (effectbuffer_mode == 3 || effectbuffer_mode == 4){
      effectbuffer_mode = 2;
    }

    //undo ugly output modes
    if (output_mode == 2 || output_mode == 3 || output_mode == 6 || output_mode == 7){
      output_mode = 0;
    }
    averagespan =0;

    for(int i=0; i<6; i++)   {
      brightness = map(spectrumValue[i],spectrumValueMin[i] ,spectrumValueMax[i],0,127); 
      if(i==5){
        if (brightness > 63){
          instantspan =  map(brightness,64,127,0,SpanWheel(span));
          averagespan = averagespan + instantspan;
          strip_buffer_1.setPixelColor(9,  Wheel(color));
          strip_buffer_1.setPixelColor(10,  Wheel(color));
          instantspan= map(brightness,64,127,SpanWheel(span),0);
          strip_buffer_2.setPixelColor(9,  Wheel(color));
          strip_buffer_2.setPixelColor(10,  Wheel(color));
        }
        else{
          instantspan =0;
          strip_buffer_1.setPixelColor(9,  Wheel(color));
          strip_buffer_1.setPixelColor(10,  Wheel(color));
          instantspan= SpanWheel(span);
          strip_buffer_2.setPixelColor(9,  Wheel(color));
          strip_buffer_2.setPixelColor(10,  Wheel(color));
        }
      }
      else{
        if (brightness > 63){
          instantspan =  map(brightness,64,127,0,SpanWheel(span));
          averagespan = averagespan + instantspan;
          strip_buffer_1.setPixelColor(2*i,  Wheel(color));
          strip_buffer_1.setPixelColor(19-2*i,  Wheel(color));
          instantspan= map(brightness,64,127,SpanWheel(span),0);
          strip_buffer_2.setPixelColor(2*i,  Wheel(color));
          strip_buffer_2.setPixelColor(19-2*i,  Wheel(color));
        }
        else{
          instantspan =0;
          strip_buffer_1.setPixelColor(2*i,  Wheel(color));
          strip_buffer_1.setPixelColor(19-2*i,  Wheel(color));
          instantspan= SpanWheel(span);
          strip_buffer_2.setPixelColor(2*i,  Wheel(color));
          strip_buffer_2.setPixelColor(19-2*i,  Wheel(color));
        }
      }
      if (i<4){
        brightness = map((spectrumValue[i+1] + spectrumValue[i]) >> 1,(spectrumValueMin[i] + spectrumValueMin[i+1]) >> 1 ,(spectrumValueMax[i] + spectrumValueMax[i+1]) >> 1,0,127); 
        if (brightness > 63){
          instantspan =  map(brightness,64,127,0,SpanWheel(span));
          averagespan = averagespan + instantspan;
          strip_buffer_1.setPixelColor(19-(2*i+1),  Wheel(color));
          strip_buffer_1.setPixelColor(2*i+1,  Wheel(color));
          instantspan= map(brightness,64,127,SpanWheel(span),0);
          strip_buffer_2.setPixelColor(19-(2*i+1),  Wheel(color));
          strip_buffer_2.setPixelColor(2*i+1,  Wheel(color));
        }
        else{
          instantspan =0;
          strip_buffer_1.setPixelColor(19-(2*i+1),  Wheel(color));
          strip_buffer_1.setPixelColor(2*i+1,  Wheel(color));
          instantspan= SpanWheel(span);
          strip_buffer_2.setPixelColor(19-(2*i+1),  Wheel(color));
          strip_buffer_2.setPixelColor(2*i+1,  Wheel(color));
        }
      }
    }
  }
  else if(effect_mode == 1){
    auto_pump_mode=0;

    //buffer modes 3 and 4 are ugly with effect 0 so force switch it
    if (effectbuffer_mode == 3 || effectbuffer_mode == 4){
      effectbuffer_mode = 2;
    }

    //undo ugly output modes
    if (output_mode == 2 || output_mode == 3 || output_mode == 6 || output_mode == 7){
      output_mode = 0;
    }
    averagespan =0;

    brightness = map(spectrumValue[0]*.3,spectrumValueMin[0]*.3,spectrumValueMax[0]*.3,0,127); 
    if (brightness > 63){
      instantspan =  map(brightness,64,127,SpanWheel(span),0);
    }
    strip_buffer_2.setPixelColor(0,  Wheel(color));
    if (brightness > 63){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip_buffer_1.setPixelColor(0,  Wheel(color));

    brightness = map(spectrumValue[0]*.6,spectrumValueMin[0]*.6,spectrumValueMax[0]*.6,0,127); 
    if (brightness > 63){
      instantspan =  map(brightness,64,127,SpanWheel(span),0);
    }
    strip_buffer_2.setPixelColor(1,  Wheel(color));
    if (brightness > 63){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip_buffer_1.setPixelColor(1,  Wheel(color));

    for(int i=0; i<5; i++)   {
      brightness = map(spectrumValue[i],spectrumValueMin[i] ,spectrumValueMax[i],0,127); 
      if (brightness > 63){
        instantspan =  map(brightness,64,127,SpanWheel(span),0);
      }
      strip_buffer_2.setPixelColor(i*3+2,  Wheel(color));
      if (brightness > 63){
        instantspan =  map(brightness,64,127,0,SpanWheel(span));
        averagespan = averagespan + instantspan;
      }
      strip_buffer_1.setPixelColor(i*3+2,  Wheel(color));

      brightness = map(spectrumValue[i+1] * .3 + spectrumValue[i] * .6,spectrumValueMin[i] * .6+ spectrumValueMin[i+1] * .3 ,spectrumValueMax[i] *.6 + spectrumValueMax[i+1] * .3,0,127); 
      if (brightness > 63){
        instantspan =  map(brightness,64,127,SpanWheel(span),0);
      }
      strip_buffer_2.setPixelColor(i*3+3,  Wheel(color));
      if (brightness > 63){
        instantspan =  map(brightness,64,127,0,SpanWheel(span));
        averagespan = averagespan + instantspan;
      }
      strip_buffer_1.setPixelColor(i*3+3,  Wheel(color));

      brightness = map(spectrumValue[i+1] * .6 + spectrumValue[i] * .3,spectrumValueMin[i] * .3+ spectrumValueMin[i+1] * .6 ,spectrumValueMax[i] *.3 + spectrumValueMax[i+1] * .6,0,127); 
      if (brightness > 63){
        instantspan =  map(brightness,64,127,SpanWheel(span),0);
      }
      strip_buffer_2.setPixelColor(i*3+4,  Wheel(color));
      if (brightness > 63){
        instantspan =  map(brightness,64,127,0,SpanWheel(span));
        averagespan = averagespan + instantspan;
      }
      strip_buffer_1.setPixelColor(i*3+4,  Wheel(color));

    }
    brightness = map(spectrumValue[5],spectrumValueMin[5] ,spectrumValueMax[5],0,127); 
    if (brightness > 63){
      instantspan =  map(brightness,64,127,SpanWheel(span),0);
    }
    strip_buffer_2.setPixelColor(17,  Wheel(color));
    if (brightness > 63){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip_buffer_1.setPixelColor(17,  Wheel(color));

    brightness = map(spectrumValue[5]*.6,spectrumValueMin[5]*.6,spectrumValueMax[5]*.6,0,127); 
    if (brightness > 63){
      instantspan =  map(brightness,64,127,SpanWheel(span),0);
    }
    strip_buffer_2.setPixelColor(18,  Wheel(color));
    if (brightness > 63){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip_buffer_1.setPixelColor(18,  Wheel(color));

    brightness = map(spectrumValue[5]*.3,spectrumValueMin[5]*.3,spectrumValueMax[5]*.3,0,127); 
    if (brightness > 63){
      instantspan =  map(brightness,64,127,SpanWheel(span),0);
    }
    strip_buffer_2.setPixelColor(19,  Wheel(color));
    if (brightness > 63){
      instantspan =  map(brightness,64,127,0,SpanWheel(span));
      averagespan = averagespan + instantspan;
    }
    strip_buffer_1.setPixelColor(19,  Wheel(color));
  }

  else if (effect_mode == 2){

    brightness = map(ytilt, 0, 254,0, 127);

    instantspan =  map(brightness,0,127,SpanWheel(span),0);
    for( i=0; i<strip_buffer_2.numPixels(); i++)     {
      strip_buffer_2.setPixelColor(i,  Wheel(color));
    }

    instantspan =  map(brightness,0,127,0,SpanWheel(span));
    for( i=0; i<strip_buffer_1.numPixels(); i++)     {
      strip_buffer_1.setPixelColor(i,  Wheel(color));
    }
  }
  else if (effect_mode == 3){
    brightness=127;
    byte tempytilt = map(ytilt, 0, 254,0, 20);

    instantspan =  map(tempytilt,0,20,SpanWheel(span),0);
    for( i=0; i<tempytilt; i++)   {
      strip_buffer_2.setPixelColor(i,  Wheel(color));
    }
    for(int i=tempytilt; i<strip_buffer_2.numPixels(); i++) strip_buffer_2.setPixelColor(i, 0);

    instantspan =  map(tempytilt,0,20,0,SpanWheel(span));
    for( i=0; i<tempytilt; i++)   {
      strip_buffer_1.setPixelColor(i,  Wheel(color));
    }
    for(int i=tempytilt; i<strip_buffer_1.numPixels(); i++) strip_buffer_1.setPixelColor(i, 0);

  }
  else if (effect_mode == 4){
    brightness=127;

    byte tempytilt = map(ytilt, 0, 254,0, 40);
    instantspan =  map(tempytilt,0,40,SpanWheel(span),0);
    if (tempytilt < 21){
      for( i=0; i<tempytilt; i++)  strip_buffer_2.setPixelColor(i, Wheel(color));
      for(int i=tempytilt; i<strip_buffer_2.numPixels(); i++) strip_buffer_2.setPixelColor(i, 0);
    }
    else{
      tempytilt = tempytilt -21;
      for( i=20; i>tempytilt; i--)  strip_buffer_2.setPixelColor(i,Wheel(color));
      for(int i=tempytilt; i>-1; i--) strip_buffer_2.setPixelColor(i, 0);
    }

    tempytilt = map(ytilt, 0, 254,0, 40);
    instantspan =  map(tempytilt,0,40,0,SpanWheel(span));
    if (tempytilt < 21){
      for( i=0; i<tempytilt; i++)  strip_buffer_1.setPixelColor(i, Wheel(color));
      for(int i=tempytilt; i<strip_buffer_1.numPixels(); i++) strip_buffer_1.setPixelColor(i, 0);
    }
    else{
      tempytilt = tempytilt -21;
      for( i=20; i>tempytilt; i--)  strip_buffer_1.setPixelColor(i,Wheel(color));
      for(int i=tempytilt; i>-1; i--) strip_buffer_1.setPixelColor(i, 0);
    }

  }
  else if (effect_mode ==5){
    brightness=127;
    byte tempytilt = map(ytilt, 0,254,0, 21);

    for( i=0; i<strip_buffer_2.numPixels(); i++) strip_buffer_2.setPixelColor(i, 0);
    instantspan =  map(tempytilt,0,21,SpanWheel(span),0);
    if (tempytilt < 21 and tempytilt >0)  strip_buffer_2.setPixelColor(tempytilt-1, Wheel(color));

    for( i=0; i<strip_buffer_1.numPixels(); i++) strip_buffer_1.setPixelColor(i, 0);
    instantspan =  map(tempytilt,0,21,0,SpanWheel(span));
    if (tempytilt < 21 and tempytilt >0)  strip_buffer_1.setPixelColor(tempytilt-1, Wheel(color));
  }
  else if (effect_mode == 6){
    brightness=127;
    for( i=0; i<strip_buffer_1.numPixels(); i++) strip_buffer_1.setPixelColor(i, Wheel(color));
    byte tempytilt = map(ytilt, 0, 254,0, 23);

    instantspan =  map(tempytilt,0,21,SpanWheel(span),0);
    if (tempytilt < 21 and tempytilt >0)  strip_buffer_2.setPixelColor(tempytilt-1, 0);
    if (tempytilt < 22 and tempytilt >1)  strip_buffer_2.setPixelColor(tempytilt-2, 0);
    if (tempytilt < 23 and tempytilt >2)  strip_buffer_2.setPixelColor(tempytilt-3, 0);

    instantspan =  map(tempytilt,0,21,0,SpanWheel(span));
    if (tempytilt < 21 and tempytilt >0)  strip_buffer_1.setPixelColor(tempytilt-1, 0);
    if (tempytilt < 22 and tempytilt >1)  strip_buffer_1.setPixelColor(tempytilt-2, 0);
    if (tempytilt < 23 and tempytilt >2)  strip_buffer_1.setPixelColor(tempytilt-3, 0);

  }
  else if (effect_mode == 7){

    byte startingpixel=0;
    byte endingpixel=0;

    switch (active_segment){
    case 1:
      startingpixel=0;
      endingpixel=14;
      break;
    case 2:
      startingpixel=14;
      endingpixel=20;
      break;
    case 3:
      startingpixel=0;
      endingpixel=8;
      break;
    case 4:
      startingpixel=8;
      endingpixel=16;
      break;
    case 5:
      startingpixel=16;
      endingpixel=20;
      break;
    }

    brightness = map(ytilt, 0, 254,0, 127);


    instantspan =  map(brightness,0,127,SpanWheel(span),0);
    for( i=0; i<strip_buffer_2.numPixels(); i++)     {
      if( i < endingpixel && i >= startingpixel){
        strip_buffer_2.setPixelColor(i,  Wheel(color));
      }
      else{
        strip_buffer_2.setPixelColor(i,  0);
      }
    }

    instantspan =  map(brightness,0,127,0,SpanWheel(span));
    for( i=0; i<strip_buffer_1.numPixels(); i++)     {
      if( i < endingpixel && i >= startingpixel){
        strip_buffer_1.setPixelColor(i,  Wheel(color));
      }
      else{
        strip_buffer_1.setPixelColor(i,  0);
      }
    }

  } 
  else if (effect_mode == 8){
    auto_pump_mode=0;

    if (effectbuffer_mode != 0){
      effectbuffer_mode = 0;
    }

    brightness=127;
    for( i=0; i<strip_buffer_1.numPixels(); i++)     {
      instantspan =  map(i,0,19,0,SpanWheel(span));
      strip_buffer_1.setPixelColor(i,  Wheel(color));
    }
  }

  if (overlay_primer!=0){
    //fadeout
    if(overlay_primer == 1){
      if(nunchuk.accelX > 1000||nunchuk.accelY > 1000 ||nunchuk.accelZ > 1000){
        overlay_starting_time =millis();
        overlay_duration=500;
        fade = 7;
        //effect_mode = 8;  leave the mode alone for fading back into the same mode
        overlay_mode=overlay_primer;
      }
    }//fadeduring
    else if(overlay_primer == 2){
      overlay_starting_time =millis();
      overlay_duration=100;
      overlay_mode=overlay_primer;
    }//fade to idle
    else if(overlay_primer == 3){
      if(nunchuk.accelX > 1000||nunchuk.accelY > 1000 ||nunchuk.accelZ > 1000){
        overlay_starting_time =millis();
        overlay_duration=500;
        effect_mode=8;
        effectbuffer_mode=0;
        fade=0;
        overlay_mode=overlay_primer;
      }
    }//fade to on
    else if(overlay_primer == 4){
      if(nunchuk.accelX > 1000||nunchuk.accelY > 1000 ||nunchuk.accelZ > 1000){
        overlay_starting_time =millis();
        overlay_duration=500;
        fade = 0;
        effect_mode=9;//check later
        overlay_mode=overlay_primer;
      } 
      else if(overlay_primer ==5){ //camera primer
        overlay_starting_time =millis();
        overlay_duration=100;
        overlay_mode=overlay_primer;
      }
    }
  }

  if(overlay_mode!=0){
    //actually overlay the pixel array
    unsigned long int currenttime = millis();
    if(currenttime<overlay_starting_time+overlay_duration){
      //during overlay
      byte tempfade = fade;
      byte tempbrightness = brightness;

      //overlay fade equation, change later to make more logorithmic
      overlay_brightness=map(currenttime,overlay_starting_time,overlay_starting_time+overlay_duration,96,0);
      fade=0;

      //dont flood the array when fading out
      if(overlay_mode!=1){
        brightness=overlay_brightness;
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
        for( i=0; i<strip_buffer_1.numPixels(); i++)     {
          strip_buffer_1.pixels[i*3+1] = strip_buffer_1.pixels[i*3+1] | r ;
          strip_buffer_2.pixels[i*3+1] = strip_buffer_2.pixels[i*3+1] | r ;
          strip_buffer_1.pixels[i*3]= strip_buffer_1.pixels[i*3] | g;
          strip_buffer_2.pixels[i*3]= strip_buffer_2.pixels[i*3] | g;
          strip_buffer_1.pixels[i*3+2] = strip_buffer_1.pixels[i*3+2] |b  ;
          strip_buffer_2.pixels[i*3+2] = strip_buffer_2.pixels[i*3+2] |b  ;
        }
      }
      fade=tempfade;
      brightness=tempbrightness;
    }
    else{
      overlay_mode=0;
    }
  }

  //output to the strips

  if (effect_mode == 7){
    output(B00000000);
  }
  else
  {
    switch (output_mode){
    case 0: //down
      output(B00000000);
      break;
    case 1: //down left
      output(B00001001);
      break; 
    case 2:  //left
      output(B00000011);
      break;
    case 3: //up left
      output(B00001010);
      break;
    case 4: //up 
      output(B00001111);
      break;
    case 5: //up right
      output(B00000110);
      break;
    case 6: //right
      output(B00001100);
      break;
    case 7: //down right
      output(B00000101);
      break;
    }
  }

  //service LCD display, do this after overlay code has ran so it has all the data to work with
  updatedisplay();    

}

int gesture(int inputvalue, int itemrange){
  //inputvalue rotate code
  int currentvalue;
  if (effect_mode == 0 || effect_mode ==1|| effect_mode ==8){
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

  if ((effect_mode == 7 && active_segment > 2) || effect_mode != 7){
    digitalWrite(strip_1,LOW);
    if (bitRead(w,0) == 1){
      if (effectbuffer_mode == 1 || effectbuffer_mode == 3){
        strip_buffer_2.showCompileTime<clockPin, dataPin>(); 
      }
      else{
        strip_buffer_1.showCompileTime<clockPin, dataPin>(); 
      }
    }
    else{
      if (effect_mode == 0){
        if (effectbuffer_mode == 1 || effectbuffer_mode == 3){
          strip_buffer_2.showCompileTimeFold<clockPin, dataPin>();
        }
        else{
          strip_buffer_1.showCompileTimeFold<clockPin, dataPin>();
        }
      }
      else{
        if (effectbuffer_mode == 1 || effectbuffer_mode == 3){
          strip_buffer_2.showCompileTimeFlip<clockPin, dataPin>();
        }
        else{
          strip_buffer_1.showCompileTimeFlip<clockPin, dataPin>();
        }
      }
    }
    digitalWrite(strip_1,HIGH);

    digitalWrite(strip_4,LOW);
    if (bitRead(w,3)== 1){
      if (effectbuffer_mode == 1 || effectbuffer_mode == 4){
        strip_buffer_2.showCompileTime<clockPin, dataPin>(); 
      }
      else{
        strip_buffer_1.showCompileTime<clockPin, dataPin>(); 
      }
    }
    else{
      if (effect_mode == 0){
        if (effectbuffer_mode == 1 || effectbuffer_mode == 4){
          strip_buffer_2.showCompileTimeFold<clockPin, dataPin>();
        }
        else{
          strip_buffer_1.showCompileTimeFold<clockPin, dataPin>();
        }
      }
      else{
        if (effectbuffer_mode == 1 || effectbuffer_mode == 4){
          strip_buffer_2.showCompileTimeFlip<clockPin, dataPin>();
        }
        else{
          strip_buffer_1.showCompileTimeFlip<clockPin, dataPin>();
        }
      }
    }
    digitalWrite(strip_4,HIGH);
  }
  else{
    //blank them
    digitalWrite(strip_1,LOW);
    digitalWrite(strip_4,LOW);

    strip_buffer_1.showCompileTimeBlank<clockPin, dataPin>();
    digitalWrite(strip_1,HIGH);
    digitalWrite(strip_4,HIGH);

  }

  if ((effect_mode == 7 && active_segment < 3) || effect_mode != 7){
    digitalWrite(strip_2,LOW);
    if (bitRead(w,1)== 1){
      if (effectbuffer_mode == 2 || effectbuffer_mode == 4){
        strip_buffer_2.showCompileTime<clockPin, dataPin>(); 
      }
      else{
        strip_buffer_1.showCompileTime<clockPin, dataPin>(); 
      }
    }
    else{
      if (effect_mode == 0){
        if (effectbuffer_mode == 2 || effectbuffer_mode == 4){
          strip_buffer_2.showCompileTimeFold<clockPin, dataPin>();
        }
        else{
          strip_buffer_1.showCompileTimeFold<clockPin, dataPin>();
        }
      }
      else{
        if (effectbuffer_mode == 2 || effectbuffer_mode == 4){
          strip_buffer_2.showCompileTimeFlip<clockPin, dataPin>();
        }
        else{
          strip_buffer_1.showCompileTimeFlip<clockPin, dataPin>();
        }
      }
    }
    digitalWrite(strip_2,HIGH);



    digitalWrite(strip_3,LOW);
    if (bitRead(w,2)== 1){
      if (effectbuffer_mode == 2 || effectbuffer_mode == 3){
        strip_buffer_2.showCompileTime<clockPin, dataPin>(); 
      }
      else{
        strip_buffer_1.showCompileTime<clockPin, dataPin>(); 
      }
    }
    else{
      if (effect_mode == 0){
        if (effectbuffer_mode == 2 || effectbuffer_mode == 3){
          strip_buffer_2.showCompileTimeFold<clockPin, dataPin>();
        }
        else{
          strip_buffer_1.showCompileTimeFold<clockPin, dataPin>();
        }
      }
      else{
        if (effectbuffer_mode == 2 || effectbuffer_mode == 3){
          strip_buffer_2.showCompileTimeFlip<clockPin, dataPin>();
        }
        else{
          strip_buffer_1.showCompileTimeFlip<clockPin, dataPin>();
        }
      }
    }
    digitalWrite(strip_3,HIGH);
  }
  else{
    //blank them
    digitalWrite(strip_2,LOW);
    digitalWrite(strip_3,LOW);

    strip_buffer_2.showCompileTimeBlank<clockPin, dataPin>();

    digitalWrite(strip_2,HIGH);
    digitalWrite(strip_3,HIGH);
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

  //normal color calculations
  if(overlay_mode!=1){
    r = r*brightness/127;
    g = g*brightness/127;
    b = b*brightness/127;
    r = r >> fade;
    g = g >> fade;
    b = b >> fade;
  }
  else{
    //pretty fadeout for overlay mode 1
    r = r*min(overlay_brightness,brightness)/127;
    g = g*min(overlay_brightness,brightness)/127;
    b = b*min(overlay_brightness,brightness)/127;

  }
  return(strip_buffer_1.Color( r,g ,b ));
}


void readserial(){

  //wixel recieving
  while(Serial1.available()){

    switch (Serial1.peek()){
    case SET_COLOR:
    case SET_SPAN:
    case SET_FADE_BRIGHTNESS:
    case BATTERY_LEVEL:
      serial1bufferpointer=0;
      serial1payloadsize=2;
      break;
    case TRIPLE_TAP:
      serial1bufferpointer=0;
      serial1payloadsize=0;
      break;
    }

    serial1buffer[serial1bufferpointer] = Serial1.read(); //load a character
    if(serial1bufferpointer == serial1payloadsize){//all payloads are size 2
      switch (serial1buffer[0]){
      case BATTERY_LEVEL:
        {
          disc_voltage  = (serial1buffer[1] << 6) | (serial1buffer[2] >> 1);
          heartbeat = millis();
          break;
        }
      case TRIPLE_TAP:
        {
          switch(effect_mode){
          case 0:
            effect_mode = 8;
            break;
          case 8:
            effect_mode = 0;
            break;
          default:
            effect_mode = 0;
          }
          break;
        }
      case SET_COLOR:
        {
          int tempcolor  = (serial1buffer[1] << 6) | (serial1buffer[2] >> 1);
          if (tempcolor > 385){
            last_set_color=tempcolor-386;
          }
          else{
            overlay_primer = 5; //pulse 
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
          //prime overlay on pacman open
          if (fade == 0){
            overlay_primer = 3;
            effect_mode=8;
          }
          Serial1.write(SET_FADE_BRIGHTNESS);
          Serial1.write(fade+8);
          Serial1.write(brightness+127);//notused padding
        }
        break;
      default:
        serial1buffer[0] = 0xff;
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
    case SET_COLOR:
      serial2bufferpointer=0;
      serial2payloadsize=4; 
      break;
    case SET_FRAME1:
    case SET_FRAME2:
      serial2bufferpointer=0;
      serial2payloadsize=80; 
      break;
    case TEXTING_REPLY:
      serial2bufferpointer=0;
      serial2payloadsize=0; 
      break;
    }

    serial2buffer[serial2bufferpointer] = Serial2.read(); //load a character
    if(serial2bufferpointer == serial2payloadsize){    //all payloads are size 2
      switch (serial2buffer[0]){
      case SET_COLOR:
        Serial2.write(CONFIRMED);
        {
          int temp  = (serial2buffer[1] << 6) | (serial2buffer[2] >> 1);
          color = temp;
          latch_data = temp;//copy into latch buffer incase a new color comes in while gestureing
          temp=( serial2buffer[3] << 6) | (serial2buffer[4] >> 1);
          span=temp;
          latch_data = temp;//copy into latch buffer incase a new color comes in while gestureing
          //turn on to static mode full bright when disabled
          if (fade == 7){
            effect_mode=8;
            fade = 0;
          }
          ring_timer=millis();
          break;
        }
      case SET_FRAME1:
        display_timer=0;

        ring_timer=millis();
        Serial2.write(CONFIRMED);
        {
          memcpy(frame1,&serial2buffer[1],sizeof(frame1));
          break;
        }
      case SET_FRAME2:
        Serial2.write(CONFIRMED);
        {
          memcpy(frame2,&serial2buffer[1],sizeof(frame2));
          display_timer=millis();
          frame_timer= display_timer;
          frame_mode = 1;
          //turn on to static mode full bright when disabled
          if (fade == 7){
            effect_mode=8;
          }
          fade = 0;
          break;
        }
      case TEXTING_REPLY:
        {
          Serial2.println(color); //COLOR1
          Serial2.println(SpanWheel(span));//COLOR2
          Serial2.println(fps_last); //FPS
          Serial2.println(bpm_period);  //BPM
          Serial2.println(jacket_voltage); //voltage
          Serial2.println(disc_voltage); //voltage
          Serial2.println(millis()); //uptime
          Serial2.println(beats);
          Serial2.println(eeprom_time_starting); //minutes
          Serial2.println(eeprom_beats_starting);
          break;
        }
      default:
        serial2buffer[0] = 0xff;
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
  if(nunchuk.analogMagnitude > 35){
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
  if (dpadtemp == 0x00 || dpadtemp !=dpad_previous){
    dpadTimer = millis();
    dpad = 0x00;
  }
  if (millis() - dpadTimer > BUTTONDELAY ){
    dpad = dpadtemp;
  }
  dpad_previous = dpadtemp;

  //nunchuck unplugged code
  if(nunchuk.pluggedin == false){
    effect_mode = 0;
    output_mode = 6;  
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

  if (auto_pump == true){

    ytilt = map((millis()-bpm_starting_time )% (bpm_period >> auto_pump_multiplier), 0, (bpm_period >> auto_pump_multiplier), 0,254);
    ytilt = ytilt * 4;
    if( ytilt < 256){
      ytilt = 0;
    }
    else{
      ytilt = ytilt -256;
      if( ytilt > 254){
        ytilt = 254;
      }
    }
  }
  else{
    ytilt = yfilter.process(nunchuk.accelY);
    ytilt = constrain(ytilt, 500, 600);
    ytilt = map(ytilt, 500, 600,0, 254);

  }

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

  if(overlay_mode == 0 || overlay_mode == 4){
    //set fade before brightness
    if (last_set_fade != fade || last_set_brightness != 127){
      Serial1.write(SET_FADE_BRIGHTNESS);
      Serial1.write(fade); //data small enough (0-7)it wont collide
      Serial1.write(127+127); //encode data to avoid collisions 0-127 moved to 127-254
    }

  }
  else if(overlay_mode == 1){
    if (last_set_brightness != overlay_brightness || last_set_fade != 0){
      Serial1.write(SET_FADE_BRIGHTNESS);
      Serial1.write(0); //data small enough (0-7)it wont collide
      Serial1.write(overlay_brightness+127); //encode data to avoid collisions 0-127 moved to 127-254
    }
  }  
  else if(overlay_mode == 3){
    int tempbrightness = max(overlay_brightness ,127 >> fade);
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
  //its important that motion effects write buffer 1 last so that instantspan is left set for the helmet to follow
  //if buffer2 is written last the helmet will be inverted (since it will be following buffer 2)

  //save brightness values for later and set to max val
  byte tempfade = fade;
  byte tempbrightness = brightness;
  if (fade < 7){
    fade = 0;
  }
  else{
    if(ring_timer + RINGTIMEOUT > millis() ||  (ring_timer + RINGTIMEOUT*2 < millis() & ring_timer + RINGTIMEOUT *3 > millis()   )){
      fade=0;
    } 
  }
  brightness=127;

  //extract RGB values from color

  //exaggerate the color change by dividing by less segments than we have, but we have to
  //check the value versus min and max to not go over
  if (effect_mode ==0 || effect_mode ==1){
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
  if(overlay_primer == 0){
    if ( zButtonDelayed == 1 && cButtonDelayed == 0){
      instantspan=SpanWheel(span);
      fade=0;
    }
    else if ( zButtonDelayed == 0 && cButtonDelayed == 1){
      instantspan=0;
      fade=0;
    }
  }

  if (overlay_mode == 1 ){
    fade=0;
    brightness = overlay_brightness;
  }


  long int tempcolor =Wheel(color);
  byte r = (tempcolor>> 8)& 0x7F;
  byte g  = (tempcolor>> 16)& 0x7F;
  byte b = (tempcolor>>0) & 0x7F;


  //build a data packet to send to the helmet
  Serial3.write(START_COMMAND);

  if(ring_timer + RINGTIMEOUT > millis() ||  (ring_timer + RINGTIMEOUT*2 < millis() & ring_timer + RINGTIMEOUT *3 > millis()   )){
    if((millis()  >> 6) & 0x01 ){ 
      r = 0;
      g = 0;
      b = 0;
    }
  }

  //output RGB to LCD backlight
  Serial3.write(r<<1);//r
  Serial3.write(g<<1);//g
  Serial3.write(b<<1);//b

  //reset to default display if timer ran out
  if(millis() - display_timer > DISPLAYTIMEOUT){
    frame_mode = 0;
  }
  else{
    //forced nyan entry
    if(color == 385 && (frame_mode != 4 && frame_mode != 3)){
      frame_mode = 3;
    }
    //forced nyan exit
    if(color != 385 && (frame_mode == 4 || frame_mode == 3)){
      frame_mode = 0;
      display_timer = 0;
    }

    if(diag_toggle_primer== true){
      switch(frame_mode){
      case 0:
        frame_mode = 5;
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        frame_mode = 0;
        break;
      case 5:
        frame_mode = 6;
        break;
      case 6:
        frame_mode = 7;
        break;
      case 7:
        frame_mode = 8;
        break;
      }

      diag_toggle_primer=false;
    }

    //only stay in diag modes while holding button
    if( frame_mode > 4 && (dpad & 0x0F) == 0x00){
      frame_mode=0;
    }

    //toggle effects otherwise
    if(frame_toggle_primer==true || millis()-frame_timer >( FRAMETIMEOUT >> animation_speed)){
      switch(frame_mode){
      case 1:
        frame_mode = 2;
        break;
      case 2:
        frame_mode = 1;
        break;
      case 3:
        frame_mode = 4;
        break;
      case 4:
        frame_mode = 3;
        break;
      }
      frame_toggle_primer=false;
      frame_timer =millis();
    }
  }



  switch (frame_mode){
  case 5:
  case 0:  //EQ mode
    Serial3.print(effect_mode);
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
    Serial3.print(output_mode);
    Serial3.print(tempfade);
    //update 2nd  line of LCD screen R values
    for (byte i=0; i<20; i++ ) {
      Serial3.write((strip_buffer_1.pixels[i*3+1] & 0x7F)>>4);
    }
    // update 3rd  line of LCD screen G values
    for (byte i=0; i<20; i++ ) {
      Serial3.write((strip_buffer_1.pixels[i*3]& 0x7F)>>4);
    }
    //update 4th  line of LCD screen B values
    for (byte i=0; i<20; i++ ) {
      Serial3.write((strip_buffer_1.pixels[i*3+2]& 0x7F) >>4 );
    }
    break;
  case 1://Text 1
    for (byte i=0; i<80; i++ ) {
      Serial3.write(frame1[i]);
    }
    break;
  case 2://Text 2
    for (byte i=0; i<80; i++ ) {
      Serial3.write(frame2[i]);
    }
    break;
  case 3://nyan1
    Serial3.print(F("-_-_-_-_,------,    _-_-_-_-|   /\\_/\\   -_-_-_-~|__( ^ .^)  _-_-_-_-  \"\"  \"\"    "));
    break;
  case 4://nyan2
    Serial3.print(F("_-_-_-_-,------,    -_-_-_-_|   /\\_/\\   --_-_-_~|__(^ .^ )  -_-_-_-_ \"\"  \"\"     "));
    break;
  case 6://announcement
    {
      Serial3.print(F("Placeholder         Placeholder         Placeholder         Placeholder         "));
    }
    break;
  case 7://stats 1
    {
      //scratch pad for numbers
      char temp[15];

      //line1
      dtostrf(jacket_voltage * 0.01986824769,5,2,temp);
      Serial3.print("    Suit: ");
      Serial3.print(temp);
      Serial3.print("V    ");
      //line2
      dtostrf(disc_voltage * 0.01603174603,5,2,temp);
      Serial3.print("    Disc: ");
      Serial3.print(temp);
      Serial3.print("V    ");
      //line3
      Serial3.print("Uptime: ");
      long int timeNow =millis();                  
      int hours = timeNow  / 3600000;                    
      int minutes = (timeNow  % 3600000) / 60000;
      int seconds = ((timeNow % 3600000) % 60000) / 1000;
      int fractime = (((timeNow % 3600000) % 60000) % 1000);
      sprintf(temp, "%02d:%02d:%02d.%03d", hours, minutes, seconds, fractime);
      Serial3.print(temp);
      //line4
      Serial3.print("Beats:  ");
      dtostrf(beats,12,0,temp);
      Serial3.print(temp);

    }
    break;
  case 8:
    {
      //scratch pad for numbers
      char temp[15];
      //line1
      dtostrf(fps_last,3,0,temp);
      Serial3.print(" FPS: ");
      Serial3.print(temp);
      Serial3.print("  BPM: ");
      if(bpm_period==1000){
        Serial3.print(" --");
      }
      else{
        dtostrf(60000/bpm_period,3,0,temp); 
        Serial3.print(temp);
      }
      Serial3.print(" ");
      //line2
      Serial3.print("  Lifetime Stats:   ");
      //line3
      Serial3.print("Uptime: ");
      long int timeNow =(eeprom_time_starting *60)+ (millis() /1000) ;
      int days = timeNow / 86400 ;                                
      int hours = (timeNow % 86400) / 3600;                    
      int minutes = ((timeNow % 86400) % 3600) / 60;
      int seconds = (((timeNow % 86400) % 3600) % 60);
      sprintf(temp, "%02dD %02d:%02d:%02d", days, hours, minutes, seconds);
      Serial3.print(temp);
      //line4
      Serial3.print("Beats: ");
      dtostrf(eeprom_beats_starting + beats,13,0,temp);
      Serial3.print(temp);
    }
    break;
  }




  //calculate indicator LEDs and related things

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
      if (overlay_primer != 0 ||(auto_pump_mode != 0 && DPAD_DOWN)){
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
  if ( effect_mode == 8){ 
    //do nothing
  }
  //EQ modes
  else if ( effect_mode == 0 || effect_mode == 1){  
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
      //run oncon peak of pump
      if(beat_completed== true){

        switch(auto_pump_mode){
        case 0:
          auto_pump = false;
          break;
        case 1:
          auto_pump = true;
          auto_pump_multiplier = 0;
          break;
        case 2:
          auto_pump_multiplier = 1;
          break;
        case 3:
          auto_pump_multiplier = 2;
          break;
        case 4:
          auto_pump_multiplier = 3;
          break;
        default:
          auto_pump_multiplier = 3;
          auto_pump_mode = 4;
        }

        if(auto_pump == true){
          //only stay in turbo modes while holding button
          if(auto_pump_multiplier > 0 && (dpad & 0x0F) == 0x00){
            auto_pump_mode=1;
            auto_pump_multiplier = 0;
          }
          if (fade == 7){
            auto_pump_mode=0;
            auto_pump = false;
          }
          //avoid time travel into the future when entering auto mode
          //if the manual entry beat comes in a few milliseconds late when transitioning it would look ugly
          if (bpm_starting_time+bpm_period > millis()){
            bpm_starting_time=millis();
          }
          else{
            bpm_starting_time=bpm_starting_time+bpm_period;
          }
        }
        else{
          //60 is minimum bpm
          if (bpm_period>1000) {
            bpm_period = 1000;
          }
          //filter the bpm
          bpm_period = bpm_period * .5 + (millis()-bpm_starting_time) *.5;
          bpm_starting_time= millis(); 
        }

        //ratchet active_segment code
        if(effect_mode == 7){
          switch (output_mode){
          case 0: //down
            active_segment++;
            if (active_segment >5){
              active_segment=1;
            }
            break;
          case 1: //down left
            active_segment--;
            if (active_segment <1){
              active_segment=2;
              output_mode=7;
            }
            break; 
          case 2:  //left
            active_segment--;
            if (active_segment <1){
              active_segment=2;
              output_mode=6;
            }
            break;
          case 3: //up left
            active_segment--;
            if (active_segment <3){
              active_segment=4;
              output_mode=5;
            }
            break;
          case 4: //up 
            active_segment--;
            if (active_segment <1){
              active_segment=5;
            }
            break;
          case 5: //up right
            active_segment++;
            if (active_segment >5){
              active_segment=4;
              output_mode=3;
            }
            break;
          case 6: //right
            active_segment++;
            if (active_segment >5){
              active_segment=4;
              output_mode=2;
            }
            break;
          case 7: //down right
            active_segment++;
            if (active_segment >2){
              active_segment=1;
              output_mode=1;
            }
            break;
          }
        }
        //mode flipping code
        if (effectbuffer_mode == 1){
          effectbuffer_mode =2;
        }
        else if(effectbuffer_mode ==2){
          effectbuffer_mode =1;
        }
        else if(effectbuffer_mode ==3){
          effectbuffer_mode =4;
        }
        else if(effectbuffer_mode ==4){
          effectbuffer_mode =3;
        }
      }
      beat_completed=false;
    }
    else if (ytilt == 254 ){
      //run once on peak of pump
      if(beat_completed== false){
        beats++;
      }
      beat_completed=true;
    }

    //if timer has ran out, set off alarm
    if (millis() - bpm_starting_time > fistpump ){
      //fist pumping alarm
      if(((millis() >> 5) & 0x01 )&& fade !=7){
        bitSet(gpio,3);
      }
    }
    //otherwise just display the current fist pumping status
    else{
      if ( beat_completed ){
        bitSet(gpio,3);
      }
    }
  }

  //flash LEDs on incoming text
  if(ring_timer + RINGTIMEOUT > millis() ||  (ring_timer + RINGTIMEOUT*2 < millis() & ring_timer + RINGTIMEOUT *3 > millis()   )){
    if((millis()  >> 6) & 0x01 ){ 
      gpio=0x0F;
    }
    else{
      gpio=0x00;
    }
  }
  Serial3.write(gpio);

  //set brightness again
  fade = tempfade;
  brightness = tempbrightness;
}















































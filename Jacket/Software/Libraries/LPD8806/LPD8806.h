#if (ARDUINO >= 100)
#include <Arduino.h>
#else
#include <WProgram.h>
#include <pins_arduino.h>
#endif

class LPD8806 {

public:


  template<unsigned int ClockPin>
    void PulseClockLine(volatile uint8_t& ClockRegister)
  {
    const byte LED_CLOCK_MASK = 1 << ClockPin;
    ClockRegister |= LED_CLOCK_MASK;
    ClockRegister &= ~LED_CLOCK_MASK;
  }

  template<unsigned int ClockPin, unsigned int DataPin>
    void TransmitBit(byte& CurrentByte, volatile uint8_t& ClockRegister, volatile uint8_t& DataRegister)
  {
    // Set the data bit
    const byte LED_DATA_MASK = 1 << DataPin;
    if (CurrentByte & 0x80)
    {
      DataRegister |= LED_DATA_MASK;
    }
    else
    {
      DataRegister &= ~LED_DATA_MASK;
    }

    // Pulse the clock line
    PulseClockLine<ClockPin>(ClockRegister);

    // Advance to the next bit to transmit
    CurrentByte = CurrentByte << 1;
  }

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
#define MAP_ARDUINO_PIN_TO_PORT_PIN(ArduinoPin) \
  ( ArduinoPin & 7 )

#define MAP_ARDUINO_PIN_TO_PORT_REG(ArduinoPin) \
    ( (ArduinoPin >= 16) ? PORTC : (((ArduinoPin) >= 8) ? PORTB : PORTD) )

      // Specify Arduino pin numbers
      template<unsigned int ClockPin, unsigned int DataPin>
        void showCompileTime()
      {
        showCompileTime<MAP_ARDUINO_PIN_TO_PORT_PIN(ClockPin), MAP_ARDUINO_PIN_TO_PORT_PIN(DataPin)>(
        MAP_ARDUINO_PIN_TO_PORT_REG(ClockPin),
        MAP_ARDUINO_PIN_TO_PORT_REG(DataPin));
      }
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeFold()
  {
    showCompileTime<MAP_ARDUINO_PIN_TO_PORT_PIN(ClockPin), MAP_ARDUINO_PIN_TO_PORT_PIN(DataPin)>(
    MAP_ARDUINO_PIN_TO_PORT_REG(ClockPin),
    MAP_ARDUINO_PIN_TO_PORT_REG(DataPin));
  }
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeFlip()
  {
    showCompileTime<MAP_ARDUINO_PIN_TO_PORT_PIN(ClockPin), MAP_ARDUINO_PIN_TO_PORT_PIN(DataPin)>(
    MAP_ARDUINO_PIN_TO_PORT_REG(ClockPin),
    MAP_ARDUINO_PIN_TO_PORT_REG(DataPin));
  }
   template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeBlank()
  {
    showCompileTime<MAP_ARDUINO_PIN_TO_PORT_PIN(ClockPin), MAP_ARDUINO_PIN_TO_PORT_PIN(DataPin)>(
    MAP_ARDUINO_PIN_TO_PORT_REG(ClockPin),
    MAP_ARDUINO_PIN_TO_PORT_REG(DataPin));
  }
#undef MAP_ARDUINO_PIN_TO_PORT_PIN
#undef MAP_ARDUINO_PIN_TO_PORT_REG
#else
  // Sorry: Didn't write an equivalent for other boards; use the other
  // overload and explicitly specify ports and offsets within those ports

#define MAP_ARDUINO_PIN_TO_PORT_PIN(ArduinoPin) \
  ( ((ArduinoPin - 53)*-1 ) & 7   )

#define MAP_ARDUINO_PIN_TO_PORT_REG(ArduinoPin) \
    ( (PORTB) )

      // Specify Arduino pin numbers
      template<unsigned int ClockPin, unsigned int DataPin>
        void showCompileTime()
      {
        showCompileTime<MAP_ARDUINO_PIN_TO_PORT_PIN(ClockPin), MAP_ARDUINO_PIN_TO_PORT_PIN(DataPin)>(
        MAP_ARDUINO_PIN_TO_PORT_REG(ClockPin),
        MAP_ARDUINO_PIN_TO_PORT_REG(DataPin));
      }
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeFold()
  {
    showCompileTimeFold<MAP_ARDUINO_PIN_TO_PORT_PIN(ClockPin), MAP_ARDUINO_PIN_TO_PORT_PIN(DataPin)>(
    MAP_ARDUINO_PIN_TO_PORT_REG(ClockPin),
    MAP_ARDUINO_PIN_TO_PORT_REG(DataPin));
  }
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeFlip()
  {
    showCompileTimeFlip<MAP_ARDUINO_PIN_TO_PORT_PIN(ClockPin), MAP_ARDUINO_PIN_TO_PORT_PIN(DataPin)>(
    MAP_ARDUINO_PIN_TO_PORT_REG(ClockPin),
    MAP_ARDUINO_PIN_TO_PORT_REG(DataPin));
  }
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeBlank()
  {
    showCompileTimeBlank<MAP_ARDUINO_PIN_TO_PORT_PIN(ClockPin), MAP_ARDUINO_PIN_TO_PORT_PIN(DataPin)>(
    MAP_ARDUINO_PIN_TO_PORT_REG(ClockPin),
    MAP_ARDUINO_PIN_TO_PORT_REG(DataPin));
  }

#undef MAP_ARDUINO_PIN_TO_PORT_PIN
#undef MAP_ARDUINO_PIN_TO_PORT_REG

#endif



  // Note: Pin template params need to be relative to their port (0..7), not Arduino pinout numbers
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTime(volatile uint8_t& ClockRegister, volatile uint8_t& DataRegister)
  {
    // Clock out the color for each LED
    byte* DataPtr = pixels;
    byte* EndDataPtr = pixels + (numLEDs * 3);

    do
    {
      byte CurrentByte = *DataPtr++;

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
    }
    while (DataPtr != EndDataPtr);

    // Clear the data line while we clock out the latching pattern
    const byte LED_DATA_MASK = 1 << DataPin;
    DataRegister &= ~LED_DATA_MASK;

    // All of the original data had the high bit set in each byte.  To latch
    // the color in, we need to clock out another LED worth of 0's for every
    // 64 LEDs in the strip apparently.
    byte RemainingLatchBytes = ((numLEDs + 63) / 64) * 3;
    do 
    {
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);

      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
    } 
    while (--RemainingLatchBytes);

  }
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeBlank(volatile uint8_t& ClockRegister, volatile uint8_t& DataRegister)
  {
    const byte LED_DATA_MASK = 1 << DataPin;
       
    byte RemainingLatchBytes = numLEDs * 3;
    do 
    {
       DataRegister &= ~LED_DATA_MASK;
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
       DataRegister |= LED_DATA_MASK;
      PulseClockLine<ClockPin>(ClockRegister);
    } 
    while (--RemainingLatchBytes);
    
    // Clear the data line while we clock out the latching pattern

    DataRegister &= ~LED_DATA_MASK;

    // All of the original data had the high bit set in each byte.  To latch
    // the color in, we need to clock out another LED worth of 0's for every
    // 64 LEDs in the strip apparently.
    RemainingLatchBytes = ((numLEDs + 63) / 64) * 3;
    do 
    {
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);

      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
    } 
    while (--RemainingLatchBytes);

  }

  // Note: Pin template params need to be relative to their port (0..7), not Arduino pinout numbers
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeFold(volatile uint8_t& ClockRegister, volatile uint8_t& DataRegister)
  {
    // Clock out the color for first half of mirror

      byte*   DataPtr = pixels + ((numLEDs / 2) * 3);
    byte*   EndDataPtr = pixels + (numLEDs * 3);


    do
    {
      byte CurrentByte = *DataPtr++;

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
    }
    while (DataPtr != EndDataPtr);

    // Clock out the color for second half of mirror
    DataPtr = pixels;
    EndDataPtr = pixels + ((numLEDs / 2) * 3);  



    do
    {
      byte CurrentByte = *DataPtr++;

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
    }
    while (DataPtr != EndDataPtr);

    // Clear the data line while we clock out the latching pattern
    const byte LED_DATA_MASK = 1 << DataPin;
    DataRegister &= ~LED_DATA_MASK;

    // All of the original data had the high bit set in each byte.  To latch
    // the color in, we need to clock out another LED worth of 0's for every
    // 64 LEDs in the strip apparently.
    byte RemainingLatchBytes = ((numLEDs + 63) / 64) * 3;
    do 
    {
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);

      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
    } 
    while (--RemainingLatchBytes);

  }



  // Note: Pin template params need to be relative to their port (0..7), not Arduino pinout numbers
  template<unsigned int ClockPin, unsigned int DataPin>
    void showCompileTimeFlip(volatile uint8_t& ClockRegister, volatile uint8_t& DataRegister)
  {
    // Start at the end, and work backwards

    byte*  DataPtr = pixels + (numLEDs * 3);
    byte*  EndDataPtr = pixels;


    do

    {
      DataPtr = DataPtr - 3;
      byte CurrentByte = *DataPtr++;

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);

      CurrentByte = *DataPtr++;

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);

      CurrentByte = *DataPtr++;

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);

      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);
      TransmitBit<ClockPin, DataPin>(CurrentByte, ClockRegister, DataRegister);


      DataPtr = DataPtr - 3;
    }

    while (DataPtr > EndDataPtr);




    // Clear the data line while we clock out the latching pattern
    const byte LED_DATA_MASK = 1 << DataPin;
    DataRegister &= ~LED_DATA_MASK;

    // All of the original data had the high bit set in each byte.  To latch
    // the color in, we need to clock out another LED worth of 0's for every
    // 64 LEDs in the strip apparently.
    byte RemainingLatchBytes = ((numLEDs + 63) / 64) * 3;
    do 
    {
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);

      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
      PulseClockLine<ClockPin>(ClockRegister);
    } 
    while (--RemainingLatchBytes);

  }

  LPD8806(uint16_t n, uint8_t dpin, uint8_t cpin); // Configurable pins
  LPD8806(uint16_t n); // Use SPI hardware; specific pins only
  LPD8806(void); // Empty constructor; init pins & strip length later


  void
    begin(void),
  show(void),
  setPixelColor(uint16_t n, uint8_t r, uint8_t g, uint8_t b),
  setPixelColor(uint16_t n, uint32_t c),
  updatePins(uint8_t dpin, uint8_t cpin), // Change pins, configurable
  updatePins(void),                       // Change pins, hardware SPI
  updateLength(uint16_t n);               // Change strip length
  uint16_t
    numPixels(void);
  uint32_t
    Color(byte, byte, byte),
  getPixelColor(uint16_t n);
  uint8_t
    *pixels;    // Holds LED color values (3 bytes each) + latch
private:

  uint16_t
    numLEDs,    // Number of RGB LEDs in strip
  numBytes;   // Size of 'pixels' buffer below
  uint8_t
    clkpin    , datapin,     // Clock & data pin numbers
  clkpinmask, datapinmask; // Clock & data PORT bitmasks
  volatile uint8_t
    *clkport  , *dataport;   // Clock & data PORT registers
  void
    startBitbang(void),
  startSPI(void);
  boolean
    hardwareSPI, // If 'true', using hardware SPI
  begun;       // If 'true', begin() method was previously invoked
};








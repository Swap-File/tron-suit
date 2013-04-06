#if (ARDUINO >= 100)
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

#include "MovingAverage.h"

MovingAverage::MovingAverage()
{
  _index = 0; //initialize so that we start to write at index 0
 
  for (_i=0; _i<5; _i++) {
    _values[_i] = 0; // fill the array with 0's
  }
}

int MovingAverage::process(int in) {
 
  _total= _total - _values[_index];        
  _values[_index] = in;
  // add the reading to the total:
  _total= _total + _values[_index];      
  // advance to the next position in the array:  
  _index++;               

  // if we're at the end of the array...
  if (_index >= 5)   {           
    // ...wrap around to the beginning:
    _index = 0;                          
  }

  return _total/5;
}


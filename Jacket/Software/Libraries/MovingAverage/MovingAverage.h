

#ifndef MovingAverage_H
#define MovingAverage_H

#if (ARDUINO >= 100)
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif


class MovingAverage
{
public:
  //construct without coefs
  MovingAverage();
  int process(int in);

private:
  int _values[5];
  int _index; 
  int _total;
  int _i; // just a loop counter
};

#endif


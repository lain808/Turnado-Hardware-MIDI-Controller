#include "ThumbJoystick.h"

ThumbJoystick::ThumbJoystick (uint8_t yAxisPin_)
{
  yAxisPin = yAxisPin_;
}

ThumbJoystick::~ThumbJoystick()
{

}

void ThumbJoystick::update()
{
  int16_t value = analogRead (yAxisPin);

  //Create a plateau around the centre point.
  if ((value > 512 - (JS_CENTRE_PLATEAU_VAL / 2)) &&
      (value < 512 + (JS_CENTRE_PLATEAU_VAL / 2)))
  {
    value = 512;
  }

  //if we've got a new Y-axis raw value within a range of +/-hysteresis_val, or a new centre or end value
  if ((value - JS_Y_HYSTERESIS_VAL > yAxisRawValue) ||
      (value + JS_Y_HYSTERESIS_VAL < yAxisRawValue) ||
      (value == 512 && yAxisRawValue != 512) ||
      (value == yAxisMinValue && yAxisRawValue != yAxisMinValue) ||
      (value == yAxisMaxValue && yAxisRawValue != yAxisMaxValue))
  {
    yAxisRawValue = value;

    //Serial.print(yAxisPin);
    //Serial.print(": ");
    //Serial.println(yAxisRawValue);

    //map and contrain raw value to user value of +/-127 with plateau values at each end
    if (value > 512)
      value = map (value, 512 + (JS_CENTRE_PLATEAU_VAL / 2), yAxisMaxValue - JS_EDGE_PLATEAU_VAL, 0, 127);
    else if (value < 512)
      value = map (value, yAxisMinValue + JS_EDGE_PLATEAU_VAL, 512 - (JS_CENTRE_PLATEAU_VAL / 2), -128, 0);
    else
      value = 0;

    value = constrain (value, -128, 127);

    if (value != yAxisUserValue)
    {
      yAxisUserValue = value;
      this->handle_joystick_change (*this, true);
    }

  } //if (new Y axis value)
}

void ThumbJoystick::onJoystickChange( void (*function)(ThumbJoystick &thumbJoystick, bool isYAxis) )
{
  this->handle_joystick_change = function;
}

int16_t ThumbJoystick::getYAxisValue()
{
  return yAxisUserValue;
}

bool ThumbJoystick::operator==(ThumbJoystick& t)
{
  return (this == &t);
}


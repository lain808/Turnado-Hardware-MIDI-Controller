#include "RotaryEncoder.h"
#include "SwitchControl.h"
#include "ThumbJoystick.h"

//=========================================================================
//DEV STUFF...
//#define DISABLE_JOYSTICKS 1

//=========================================================================
RotaryEncoder* knobControllersEncoders[NUM_OF_KNOB_CONTROLLERS];
ThumbJoystick* knobControllersJoysticks[NUM_OF_KNOB_CONTROLLERS];

RotaryEncoder* mixEncoder;

RotaryEncoder* lcdEncoders[NUM_OF_LCD_ENCS];

SwitchControl* presetUpButton;
SwitchControl* presetDownButton;

SwitchControl* randomiseButton;

//=========================================================================
//FIXME: could knobControllerData and mixControllerData be arrays for each MIDI channel and replace deviceParamValuesForMidiChannel?

struct KnobControllerData
{
  int16_t baseValue = 0;
  uint8_t prevBaseValue = 0;
  int16_t relativeValue = 0;
  int8_t prevRelativeValue = 0;
  int16_t combinedMidiValue = 0;
  uint8_t prevCombinedMidiValue = 0;
};

KnobControllerData knobControllerData[NUM_OF_KNOB_CONTROLLERS];
bool ignoreJsMessage[NUM_OF_KNOB_CONTROLLERS] = {false};

struct MixControllerData
{
  int16_t midiValue = 127;
  uint8_t prevMidiValue = 127;
};

MixControllerData mixControllerData;

uint8_t randomiseButtonState = 0;
bool ignoreNextRandomiseButtonRelease = false;

uint8_t presetUpButtonState = 0;
uint8_t presetDownButtonState = 0;
bool ignoreNextPresetButtonRelease = false;

enum PresetButtonType
{
  PRESET_BUTTON_TYPE_DOWN = 0,
  PRESET_BUTTON_TYPE_UP
};

//=========================================================================
void processEncoderChange (RotaryEncoder &enc, int enc_value);
void processEncoderSwitchChange (RotaryEncoder &enc);
void processPushButtonChange (SwitchControl &switchControl);
void processJoystickChange (ThumbJoystick &thumbJoystick, bool isYAxis);
void setGlobalMidiChannel (int8_t incVal);

//=========================================================================
//=========================================================================
//=========================================================================
void setupControls()
{
  for (auto i = 0; i < NUM_OF_KNOB_CONTROLLERS; i++)
  {
    knobControllersEncoders[i] = new RotaryEncoder (PINS_KNOB_CTRL_ENCS[i].pinA, PINS_KNOB_CTRL_ENCS[i].pinB, PINS_KNOB_CTRL_ENCS[i].pinSwitch);
    knobControllersEncoders[i]->onEncoderChange (processEncoderChange);
    knobControllersEncoders[i]->onSwitchChange (processEncoderSwitchChange);

    knobControllersJoysticks[i] = new ThumbJoystick (PINS_KNOB_CTRL_JOYSTICKS[i]);
    knobControllersJoysticks[i]->onJoystickChange (processJoystickChange);
  }

  mixEncoder = new RotaryEncoder (PINS_MIX_ENC.pinA, PINS_MIX_ENC.pinB, PINS_MIX_ENC.pinSwitch);
  mixEncoder->onEncoderChange (processEncoderChange);
  lcdSetSliderValue (LCD_SLIDER_MIX_INDEX, mixControllerData.midiValue);

  for (auto i = 0; i < NUM_OF_LCD_ENCS; i++)
  {
    lcdEncoders[i] = new RotaryEncoder (PINS_LCD_ENCS[i].pinA, PINS_LCD_ENCS[i].pinB, PINS_LCD_ENCS[i].pinSwitch);
    lcdEncoders[i]->onEncoderChange (processEncoderChange);
    lcdEncoders[i]->onSwitchChange (processEncoderSwitchChange);
  }

  presetUpButton = new SwitchControl (PIN_PRESET_UP_BUTTON);
  presetUpButton->onSwitchStateChange (processPushButtonChange);
  presetDownButton = new SwitchControl (PIN_PRESET_DOWN_BUTTON);
  presetDownButton->onSwitchStateChange (processPushButtonChange);

  randomiseButton = new SwitchControl (PIN_RANDOMISE_BUTTON);
  randomiseButton->onSwitchStateChange (processPushButtonChange);

  //setupSettings() must be called before setupControls() for the below to be set correctly.
  currentMidiProgramNumber = settingsData[SETTINGS_PRESET].paramData[PARAM_INDEX_START_NUM].value;
}

//=========================================================================
//=========================================================================
//=========================================================================
void updateControls()
{
  for (auto i = 0; i < NUM_OF_KNOB_CONTROLLERS; i++)
  {
    knobControllersEncoders[i]->update();

#ifndef DISABLE_JOYSTICKS
    knobControllersJoysticks[i]->update();
#endif
  }

  mixEncoder->update();

  for (auto i = 0; i < NUM_OF_LCD_ENCS; i++)
  {
    lcdEncoders[i]->update();
  }

  presetUpButton->update();
  presetDownButton->update();
  randomiseButton->update();
}

//=========================================================================
//=========================================================================
//=========================================================================
void setKnobControllerCombinedMidiValue (uint8_t index, bool sendToMidiOut)
{
  if (knobControllerData[index].relativeValue > 0)
    knobControllerData[index].combinedMidiValue = map (knobControllerData[index].relativeValue, 0, 127, knobControllerData[index].baseValue, 127);
  else if (knobControllerData[index].relativeValue < 0)
    knobControllerData[index].combinedMidiValue = map (knobControllerData[index].relativeValue, 0, -128, knobControllerData[index].baseValue, 0);
  else
    knobControllerData[index].combinedMidiValue = knobControllerData[index].baseValue;

  if (knobControllerData[index].combinedMidiValue != knobControllerData[index].prevCombinedMidiValue)
  {
    if (sendToMidiOut)
    {
      //send MIDI message
      byte channel = settingsData[index + 1].paramData[PARAM_INDEX_MIDI_CHAN].value;
      if (channel == 0)
        channel = settingsData[SETTINGS_GLOBAL].paramData[PARAM_INDEX_MIDI_CHAN].value;
      byte control = settingsData[index + 1].paramData[PARAM_INDEX_CC_NUM].value;
      byte value = knobControllerData[index].combinedMidiValue;
      sendMidiCcMessage (channel, control, value, index);

    } //if (sendToMidiOut)

    //update LCD display
    lcdSetSliderValue (index, knobControllerData[index].combinedMidiValue);

    knobControllerData[index].prevCombinedMidiValue = knobControllerData[index].combinedMidiValue;

  } //if (knobControllerData[index].combinedMidiValue != knobControllerData[index].prevCombinedMidiValue)
}

//=========================================================================
//=========================================================================
//=========================================================================
void setKnobControllerBaseValue (uint8_t index, uint8_t value, bool sendToMidiOut)
{
  knobControllerData[index].baseValue = value;

  if (knobControllerData[index].baseValue != knobControllerData[index].prevBaseValue)
  {
    setKnobControllerCombinedMidiValue (index, sendToMidiOut);
    knobControllerData[index].prevBaseValue = knobControllerData[index].baseValue;
  }
}

//=========================================================================
//=========================================================================
//=========================================================================
void setMixControllerValue (uint8_t value, bool sendToMidiOut)
{
  mixControllerData.midiValue = value;

  if (mixControllerData.midiValue != mixControllerData.prevMidiValue)
  {
    if (sendToMidiOut)
    {
      //send MIDI message
      byte channel = settingsData[SETTINGS_MIX].paramData[PARAM_INDEX_MIDI_CHAN].value;
      if (channel == 0)
        channel = settingsData[SETTINGS_GLOBAL].paramData[PARAM_INDEX_MIDI_CHAN].value;
      byte control = settingsData[SETTINGS_MIX].paramData[PARAM_INDEX_CC_NUM].value;
      byte value = mixControllerData.midiValue;
      sendMidiCcMessage (channel, control, value, DEVICE_PARAM_INDEX_MIX);

    } //if (sendToMidiOut)

    //update LCD display
    lcdSetSliderValue (LCD_SLIDER_MIX_INDEX, mixControllerData.midiValue);

    mixControllerData.prevMidiValue = mixControllerData.midiValue;

  } //if (mixControllerData.midiValue != mixControllerData.prevMidiValue)
}

//=========================================================================
//=========================================================================
//=========================================================================
void setCurrentMidiProgramNumber (int8_t incVal)
{
  //Always send MIDI program change messages here,
  //even if the new program number is the same as the previous one
  //(so that we can 'reset' the current MIDI program).

  currentMidiProgramNumber = constrain (currentMidiProgramNumber + incVal, 0, 127);

  //send MIDI message
  byte channel = settingsData[SETTINGS_PRESET].paramData[PARAM_INDEX_MIDI_CHAN].value;
  if (channel == 0)
    channel = settingsData[SETTINGS_GLOBAL].paramData[PARAM_INDEX_MIDI_CHAN].value;
  sendMidiProgramChangeMessage (channel, currentMidiProgramNumber);

  //flag to update program in LCD top bar display
  lcdTopBarProgramChanged = true;

  //Do I need to do anything to the param values? If program change resets turnado knobs
  //we should received MIDI-in CCs for these changes, but what about for dictator and mix params?
}

//=========================================================================
//=========================================================================
//=========================================================================
void setGlobalMidiChannel (int8_t incVal)
{
  uint8_t prevChan = settingsData[SETTINGS_GLOBAL].paramData[PARAM_INDEX_MIDI_CHAN].value;
  uint8_t newChan = constrain (prevChan + incVal, 1, 16);

  if (prevChan != newChan)
  {
    settingsData[SETTINGS_GLOBAL].paramData[PARAM_INDEX_MIDI_CHAN].value = newChan;
    settingsData[SETTINGS_GLOBAL].paramData[PARAM_INDEX_MIDI_CHAN].needsSavingToEeprom = true;

    //flag to update channel in LCD top bar display
    lcdTopBarChannelChanged = true;

    //update the knob controller base values (and LCD display) with values of new channel
    //for the knob controllers that are set to use the global channel

    for (uint8_t i = 0; i < NUM_OF_DEVICE_PARAMS; i++)
    {
      if (settingsData[i + 1].paramData[PARAM_INDEX_MIDI_CHAN].value == 0)
      {
        if (i < DEVICE_PARAM_INDEX_MIX)
          setKnobControllerBaseValue (i, deviceParamValuesForMidiChannel[newChan - 1][i], false);
        else
          setMixControllerValue (deviceParamValuesForMidiChannel[newChan - 1][i], false);
      }

    } //for (uint8_t i = 0; i < NUM_OF_ACTUAL_KNOB_CONTROLLERS; i++)

  } //if (prevChan != newChan)
}

//=========================================================================
//=========================================================================
//=========================================================================
uint8_t handlePresetButtonInteraction (uint8_t buttonType, uint8_t newButtonState)
{
  uint8_t thisButtonTypeState;
  uint8_t otherButtonTypeState;

  if (buttonType == PRESET_BUTTON_TYPE_DOWN)
  {
    thisButtonTypeState = presetDownButtonState;
    otherButtonTypeState = presetUpButtonState;
  }
  else
  {
    thisButtonTypeState = presetUpButtonState;
    otherButtonTypeState = presetDownButtonState;
  }

  if (newButtonState != thisButtonTypeState)
  {
    //if a button release and we don't want to ignore it
    if (newButtonState == 0 && !ignoreNextPresetButtonRelease)
    {
      //if randomise button is not being held down
      if (randomiseButtonState == 0)
      {
        //if preset up button release while preset down button isn't held, increment MIDI program number
        if (buttonType == PRESET_BUTTON_TYPE_UP && otherButtonTypeState == 0)
          setCurrentMidiProgramNumber (1);
        //if preset down button release while preset up button isn't held, decrement MIDI program number
        if (buttonType == PRESET_BUTTON_TYPE_DOWN && otherButtonTypeState == 0)
          setCurrentMidiProgramNumber (-1);
        //if other preset button is being held, resend current MIDI program number and flag to ignore next button release
        else if (otherButtonTypeState > 0)
        {
          setCurrentMidiProgramNumber (0);
          ignoreNextPresetButtonRelease = true;
        }

      } //if (randomiseButtonState == 0)

      //if randomise button is being held down
      else
      {
        if (buttonType == PRESET_BUTTON_TYPE_UP)
          setGlobalMidiChannel (1);
        else if (buttonType == PRESET_BUTTON_TYPE_DOWN)
          setGlobalMidiChannel (-1);

        ignoreNextRandomiseButtonRelease = true;
      }

    } //if (newButtonState == 0 && !ignoreNextPresetButtonRelease)

    thisButtonTypeState = newButtonState;

  }//if (newButtonState != thisButtonTypeState)

  //if both preset buttons are off, make sure the next release message won't be ignored
  if (thisButtonTypeState == 0 && otherButtonTypeState == 0)
    ignoreNextPresetButtonRelease = false;

  return thisButtonTypeState;
}

//=========================================================================
//=========================================================================
//=========================================================================
void processEncoderChange (RotaryEncoder &enc, int enc_value)
{
  //=========================================================================
  for (auto i = 0; i < NUM_OF_KNOB_CONTROLLERS; i++)
  {
    if (enc == *knobControllersEncoders[i])
    {
#ifdef DEBUG
      Serial.print ("Knob Controller ");
      Serial.print (i + 1);
      Serial.print (" encoder: ");
      Serial.println (enc_value);
#endif

      setKnobControllerBaseValue (i, constrain (knobControllerData[i].baseValue + enc_value, 0, 127), true);

    } //if (enc == *knobControllersEncoders[i])

  } //for (auto i = 0; i < NUM_OF_KNOB_CONTROLLERS; i++)

  //=========================================================================
  if (enc == *mixEncoder)
  {
#ifdef DEBUG
    Serial.print ("Mix encoder: ");
    Serial.println (enc_value);
#endif

    setMixControllerValue (constrain (mixControllerData.midiValue + enc_value, 0, 127), true);

  } //if (enc == *mixEncoder)

  //=========================================================================
  else if (enc == *lcdEncoders[LCD_ENC_CTRL])
  {
#ifdef DEBUG
    Serial.print ("LCD CTRL encoder: ");
    Serial.println (enc_value);
#endif

    lcdSetSelectedMenu (enc_value);
  }

  //=========================================================================
  else if (enc == *lcdEncoders[LCD_ENC_PARAM])
  {
#ifdef DEBUG
    Serial.print ("LCD Param encoder: ");
    Serial.println (enc_value);
#endif

    lcdSetSelectedParam (enc_value);
  }

  //=========================================================================
  else if (enc == *lcdEncoders[LCD_ENC_VAL])
  {
#ifdef DEBUG
    Serial.print ("LCD Value encoder: ");
    Serial.println (enc_value);
#endif

    lcdSetSelectedParamValue (enc_value);
  }
}

//=========================================================================
//=========================================================================
//=========================================================================
void processEncoderSwitchChange (RotaryEncoder &enc)
{
  //=========================================================================
  for (auto i = 0; i < NUM_OF_KNOB_CONTROLLERS; i++)
  {
    if (enc == *knobControllersEncoders[i])
    {
#ifdef DEBUG
      Serial.print ("Knob Controller ");
      Serial.print (i + 1);
      Serial.print (" encoder switch: ");
      Serial.println (enc.getSwitchState());
#endif

      //if switch is being turned on
      if (enc.getSwitchState() > 0)
      {
        //if knob controller joystick is currently centred
        if (knobControllerData[i].relativeValue == 0)
        {
          //Use switch to reset base value...

          //There is a 'bug' (or unexplained behaviour) with Turnado where if controlling the Turnado knob directly in software,
          //and then sending a single CC message to change the knob value which is the same value as the last MIDI message sent,
          //the value won't reset in Turnado. E.g. send a MIDI CC value of 0 with the device, turn the knob directly in Turnado
          //to any value above 0, and then send a second MIDI CC value of 0 with the device, Turnado won't respond to the MIDI CC.
          //Same as with the randomise button, it appears Turnado needs a MIDI-in state change to respond to MIDI,
          //therefore the workaround for this is to send two CCs to reset the knob value - 1 followed by 0.

          for (int8_t val = 1; val >= 0; val--)
          {
            knobControllerData[i].baseValue = val;

            if (knobControllerData[i].baseValue != knobControllerData[i].prevBaseValue)
            {
              setKnobControllerCombinedMidiValue (i, true);
              knobControllerData[i].prevBaseValue = knobControllerData[i].baseValue;
            }

          } //for (uint8_t val = 1; val >= 0; val--)

        } // if (knobControllerData[i].relativeValue)

        //if knob controller joystick is being used
        else
        {
          //Use switch to set the current combined value as the base value...
          //(would be better if this behaviour could instead be triggered by the JS switches,
          //however these aren't wired on the current prototype).

          //set the base values
          knobControllerData[i].baseValue = knobControllerData[i].combinedMidiValue;
          knobControllerData[i].prevBaseValue = knobControllerData[i].baseValue;

          //reset relative values
          knobControllerData[i].relativeValue = 0;
          knobControllerData[i].prevRelativeValue = knobControllerData[i].relativeValue;

          //Don't need to set combined MIDI value as this isn't changing (and therefore
          //don't need to send a new MIDI message or update the LCD)

          //flag to ignore knob controller joystick until it is centred again
          //(otherwise the relative value will jump with the next joystick movement)
          ignoreJsMessage[i] = true;

        } //else (relativeValue[i] != 0)

      } //if (enc.getSwitchState() > 0)

    }//if (enc == *knobControllersEncoders[i])

  } //for (auto i = 0; i < NUM_OF_KNOB_CONTROLLERS; i++)

  //=========================================================================
  if (enc == *lcdEncoders[LCD_ENC_CTRL])
  {
#ifdef DEBUG
    Serial.print ("LCD CTRL encoder swich: ");
    Serial.println (enc.getSwitchState());
#endif

    //if switch is being turned on
    if (enc.getSwitchState() > 0)
    {
      lcdToggleDisplayMode();

      //if switching away from menu display, do a delta save of settings
      if (lcdDisplayMode == LCD_DISPLAY_MODE_CONTROLS)
      {
        settingsSaveToEeprom (true);
      }

    } //if (enc.getSwitchState() > 0)

  } //if (enc == *lcdEncoders[LCD_ENC_CTRL])
}

//=========================================================================
//=========================================================================
//=========================================================================
void processPushButtonChange (SwitchControl &switchControl)
{
  //=========================================================================
  if (switchControl == *presetUpButton)
  {
#ifdef DEBUG
    Serial.print ("Preset Up Button: ");
    Serial.println (switchControl.getSwitchState());
#endif

    presetUpButtonState = handlePresetButtonInteraction (PRESET_BUTTON_TYPE_UP, switchControl.getSwitchState());

  } //if (switchControl == *presetUpButton)

  //=========================================================================
  else if (switchControl == *presetDownButton)
  {
#ifdef DEBUG
    Serial.print ("Preset Down Button: ");
    Serial.println (switchControl.getSwitchState());
#endif

    presetDownButtonState = handlePresetButtonInteraction (PRESET_BUTTON_TYPE_DOWN, switchControl.getSwitchState());

  } //else if (switchControl == *presetDownButton)

  //=========================================================================
  else if (switchControl == *randomiseButton)
  {
#ifdef DEBUG
    Serial.print ("Randomise Button: ");
    Serial.println (switchControl.getSwitchState());
#endif

    if (switchControl.getSwitchState() != randomiseButtonState)
    {
      //if a button release that we don't want to ignore
      if (switchControl.getSwitchState() == 0 && !ignoreNextRandomiseButtonRelease)
      {
        //send MIDI message
        byte channel = settingsData[SETTINGS_RANDOMISE].paramData[PARAM_INDEX_MIDI_CHAN].value;
        if (channel == 0)
          channel = settingsData[SETTINGS_GLOBAL].paramData[PARAM_INDEX_MIDI_CHAN].value;
        byte control = settingsData[SETTINGS_RANDOMISE].paramData[PARAM_INDEX_CC_NUM].value;

        //The Turnado randomise button needs a CC value change to trigger it (sending the same CC value won't do anything).
        //Therefore need to send two CC's here each with a different value.
        sendMidiCcMessage (channel, control, 127, -1);
        sendMidiCcMessage (channel, control, 0, -1);

      } //if (switchControl.getSwitchState() == 0 && !ignoreNextRandomiseButtonRelease)

      randomiseButtonState = switchControl.getSwitchState();
      ignoreNextRandomiseButtonRelease = false;

    } //if (switchControl.getSwitchState() != randomiseButtonState)

  } //else if (switchControl == *randomiseButton)

}

//=========================================================================
//=========================================================================
//=========================================================================
void processJoystickChange (ThumbJoystick &thumbJoystick, bool isYAxis)
{
  if (isYAxis)
  {
    for (auto i = 0; i < NUM_OF_KNOB_CONTROLLERS; i++)
    {
      if (thumbJoystick == *knobControllersJoysticks[i])
      {
#ifdef DEBUG
        Serial.print ("Knob Controller ");
        Serial.print (i + 1);
        Serial.print (" joystick: ");
        Serial.println (thumbJoystick.getYAxisValue());
#endif

        if (!ignoreJsMessage[i])
        {
          knobControllerData[i].relativeValue = thumbJoystick.getYAxisValue();

          if (knobControllerData[i].relativeValue != knobControllerData[i].prevRelativeValue)
          {
            setKnobControllerCombinedMidiValue (i, true);
            knobControllerData[i].prevRelativeValue = knobControllerData[i].relativeValue;
          }
        } //if (!ignoreJsMessage[i])

        else
        {
          //if the ignored joystick has been centred, no longer ignore it.
          if (thumbJoystick.getYAxisValue() == 0)
            ignoreJsMessage[i] = false;
        }

      } //if (thumbJoystick == *knobControllersJoysticks[i])

    } //for (auto i = 0; i < NUM_OF_KNOB_CONTROLLERS; i++)

  } //if (isYAxis)
}

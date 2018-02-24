// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. 
#include "Arduino.h"
#include "AudioClassV2.h"

#include "../inc/fanSound.h"
#include "../inc/fanSoundData.h"

void playFanSound(void)
{
    AudioClass& audio = AudioClass::getInstance();
    audio.startPlay((char*)fanSoundData, FAN_SOUND_DATA_SIZE);
}

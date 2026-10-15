/*
 * Copyright 2013 Google Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __CONTROLLERS_H
#define __CONTROLLERS_H

// State of MIDI controllers

const int kControllerPitch = 128;

class Controllers {
 public:
  int values_[129];

  // Neptune : valeurs fournies par la couche de voix (le DX7 d'origine les lit sur ses entrées de contrôleurs).
  int pitchbend_range_ = 2;      // demi-tons, 0..12 (msfa figeait 3)
  int wheel_pitch_depth_ = 0;    // 0..255 : profondeur de vibrato ajoutée par la molette (même unité que PMD * 165 / 64)
  int wheel_amp_depth_ = 0;      // 0..99 : profondeur de trémolo ajoutée par la molette (même unité que AMD)
};

#endif  // __CONTROLLERS_H


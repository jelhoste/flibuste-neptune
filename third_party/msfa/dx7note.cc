/*
 * Copyright 2012 Google Inc.
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

#ifdef VERBOSE
#include <algorithm>
#include <iostream>
#endif
#include <math.h>
#include "synth.h"
#include "freqlut.h"
#include "patch.h"
#include "exp2.h"
#include "controllers.h"
#include "dx7note.h"

using namespace std;

int32_t midinote_to_logfreq(int midinote) {
  const int base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
  const int step = (1 << 24) / 12;
  return base + step * midinote;
}

const int32_t coarsemul[] = {
  -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
  50331648, 53182516, 55732705, 58039632, 60145690, 62083076, 63876816,
  65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
  74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032,
  81503396, 82323963, 83117622
};

int32_t osc_freq(int midinote, int mode, int coarse, int fine, int detune) {
  // TODO: pitch randomization
  int32_t logfreq;
  if (mode == 0) {
    logfreq = midinote_to_logfreq(midinote);
    logfreq += coarsemul[coarse & 31];
    if (fine) {
      // (1 << 24) / log(2)
      logfreq += (int32_t)floor(24204406.323123 * log(1 + 0.01 * fine) + 0.5);
    }
    // This was measured at 7.213Hz per count at 9600Hz, but the exact
    // value is somewhat dependent on midinote. Close enough for now.
    logfreq += 12606 * (detune - 7);
  } else {
    // ((1 << 24) * log(10) / log(2) * .01) << 3
    logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
    logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
  }
  return logfreq;
}

const uint8_t velocity_data[64] = {
  0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
  166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202,
  205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232,
  233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251,
  252, 253, 254
};

// See "velocity" section of notes. Returns velocity delta in microsteps.
int ScaleVelocity(int velocity, int sensitivity) {
  int clamped_vel = max(0, min(127, velocity));
  int vel_value = velocity_data[clamped_vel >> 1] - 239;
  int scaled_vel = ((sensitivity * vel_value + 7) >> 3) << 4;
  return scaled_vel;
}

int ScaleRate(int midinote, int sensitivity) {
  int x = min(31, max(0, midinote / 3 - 7));
  int qratedelta = (sensitivity * x) >> 3;
#ifdef SUPER_PRECISE
  int rem = x & 7;
  if (sensitivity == 3 && rem == 3) {
    qratedelta -= 1;
  } else if (sensitivity == 7 && rem > 0 && rem < 4) {
    qratedelta += 1;
  }
#endif
  return qratedelta;
}

const uint8_t exp_scale_data[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
  80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250
};

int ScaleCurve(int group, int depth, int curve) {
  int scale;
  if (curve == 0 || curve == 3) {
    // linear
    scale = (group * depth * 329) >> 12;
  } else {
    // exponential
    int n_scale_data = sizeof(exp_scale_data);
    int raw_exp = exp_scale_data[min(group, n_scale_data - 1)];
    scale = (raw_exp * depth * 329) >> 15;
  }
  if (curve < 2) {
    scale = -scale;
  }
  return scale;
}

int ScaleLevel(int midinote, int break_pt, int left_depth, int right_depth,
    int left_curve, int right_curve) {
  int offset = midinote - break_pt - 17;
  if (offset >= 0) {
    return ScaleCurve(offset / 3, right_depth, right_curve);
  } else {
    return ScaleCurve((-offset) / 3, left_depth, left_curve);
  }
}

static const uint8_t pitchmodsenstab[] = {
  0, 10, 20, 33, 55, 92, 153, 255
};

void Dx7Note::init(const char patch[156], int midinote, int velocity) {
  detune_offset_ = 0;
  glide_offset_ = 0;
  for (int i = 0; i < 6; i++) level_offset_[i] = 0;
  fb_buf_[0] = fb_buf_[1] = 0;
  int rates[4];
  int levels[4];
  for (int op = 0; op < 6; op++) {
    int off = op * 21;
    for (int i = 0; i < 4; i++) {
      rates[i] = patch[off + i];
      levels[i] = patch[off + 4 + i];
    }
    int outlevel = patch[off + 16];
    outlevel = Env::scaleoutlevel(outlevel);
#ifdef VERBOSE
    for (int j = 8; j < 12; j++) {
      cout << (int)patch[off + j] << " ";
    }
#endif
    int level_scaling = ScaleLevel(midinote, patch[off + 8], patch[off + 9],
        patch[off + 10], patch[off + 11], patch[off + 12]);
    outlevel += level_scaling;
    outlevel = min(127, outlevel);
#ifdef VERBOSE
    cout << op << ": " << level_scaling << " " << outlevel << endl;
#endif
    outlevel = outlevel << 5;
    outlevel += ScaleVelocity(velocity, patch[off + 15]);
    outlevel = max(0, outlevel);
    int rate_scaling = ScaleRate(midinote, patch[off + 13]);
    env_[op].init(rates, levels, outlevel, rate_scaling);

    ampmodsens_[op] = patch[off + 14] & 3;  // Neptune : AMS
    int mode = patch[off + 17];
    fixed_[op] = mode != 0;
    int coarse = patch[off + 18];
    int fine = patch[off + 19];
    int detune = patch[off + 20];
    int32_t freq = osc_freq(midinote, mode, coarse, fine, detune);
    basepitch_[op] = freq;
    // cout << op << " freq: " << freq << endl;
    params_[op].phase = 0;
    params_[op].gain[1] = 0;
  }
  for (int i = 0; i < 4; i++) {
    rates[i] = patch[126 + i];
    levels[i] = patch[130 + i];
  }
  pitchenv_.set(rates, levels);
  algorithm_ = patch[134];
  int feedback = patch[135];
  fb_shift_ = feedback != 0 ? 8 - feedback : 16;
  pitchmoddepth_ = (patch[139] * 165) >> 6;
  pitchmodsens_ = pitchmodsenstab[patch[143] & 7];
  ampmoddepth_ = patch[140];  // Neptune : AMD
  opmask_ = patch[155] & 0x3f;
}

// Neptune : modulation d'amplitude (AMS / AMD, absente de msfa), profondeur de la molette, étendue du pitch bend réglable,
// opérateurs coupés, décalages d'unisson et de glissando. Le reste est le calcul d'origine.
void Dx7Note::compute(int32_t *buf, int32_t lfo_val, int32_t lfo_delay,
  const Controllers *ctrls) {
  int32_t pitchmod = pitchenv_.getsample();
  int pmdepth = pitchmoddepth_ + ctrls->wheel_pitch_depth_;
  if (pmdepth > 255) pmdepth = 255;
  uint32_t pmd = pmdepth * lfo_delay;  // Q32
  int32_t senslfo = pitchmodsens_ * (lfo_val - (1 << 23));
  pitchmod += (((int64_t)pmd) * (int64_t)senslfo) >> 39;

  // pitch bend : 14 bits, étendue en demi-tons (Q24 par octave : 1 demi-ton = (1 << 24) / 12)
  int pitchbend = ctrls->values_[kControllerPitch];
  int32_t pb = (int32_t)(((int64_t)(pitchbend - 0x2000) * ctrls->pitchbend_range_ * 1398101) >> 13);
  pitchmod += pb;

  // trémolo : atténuation en doublements Q24, au plus 7 doublements (environ 42 dB) à pleine profondeur et AMS 3
  int amd = ampmoddepth_ + ctrls->wheel_amp_depth_;
  if (amd > 99) amd = 99;
  int32_t amp_lfo = (int32_t)(((int64_t)lfo_val * (int64_t)lfo_delay) >> 24);  // 0..1 en Q24, après le délai
  int32_t att_full = (int32_t)(((int64_t)amp_lfo * amd * 7) / 99);              // en doublements Q24 (x 7 doublements)
  static const int ams_gain[4] = { 0, 85, 171, 256 };                           // 0, 1/3, 2/3, 1 (Q8)

  for (int op = 0; op < 6; op++) {
    params_[op].gain[0] = params_[op].gain[1];
    int32_t level = env_[op].getsample() + level_offset_[op];  // Neptune : décalage de niveau
    if (ampmodsens_[op] != 0 && amd != 0) {
      level -= (int32_t)(((int64_t)att_full * ams_gain[ampmodsens_[op]]) >> 8);
    }
    // Neptune : Exp2::lookup décale de (6 - x >> 24) ; hors de -14..+6 doublements ce décalage sort de 0..31 (comportement indéfini : gain énorme
    // avec un décalage de niveau très négatif, par exemple). Le niveau reste donc entre 0 et 16 doublements (au plus +12 dB au-dessus du niveau normal d'un opérateur).
    level = std::clamp(level, 0, (16 << 24) - 1);
    int32_t gain = Exp2::lookup(level - (14 * (1 << 24)));
    if (((opmask_ >> op) & 1) == 0) gain = 0;
    int32_t offset = detune_offset_ + (fixed_[op] ? 0 : glide_offset_);
    params_[op].freq = Freqlut::lookup(basepitch_[op] + pitchmod + offset);
    params_[op].gain[1] = gain;
  }
  core_.compute(buf, params_, algorithm_, fb_buf_, fb_shift_);
}

void Dx7Note::keyup() {
  for (int op = 0; op < 6; op++) {
    env_[op].keydown(false);
    pitchenv_.keydown(false);
  }
}


// Filename: arrival.cxx
// Created by:  Deepak, John, Navin (24Oct09)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) Carnegie Mellon University.  All rights reserved.
//
// All use of this software is subject to the terms of the revised 
// BSD license.  You should have received a copy of this license
// along with this source code in a file named "LICENSE."
//
////////////////////////////////////////////////////////////////////

#include "arrival.h"

Arrival::Arrival(AICharacter *ai_ch, double distance) {
  _ai_char = ai_ch;

  _arrival_distance = distance;
  _arrival_done = false;
}

Arrival::~Arrival() {
}

////////////////////////////////////////////////////////////////////
//     Function: do_arrival
//  Description: This function performs the arrival and returns an
//               arrival force which is used in the
//               calculate_prioritized function. In case the steering
//               force is zero, it resets to arrival_activate. The
//               arrival behavior works only when seek or pursue is
//               active. This function is not to be used by the user.
////////////////////////////////////////////////////////////////////
LVecBase3f Arrival::do_arrival() {
  LVecBase3f direction_to_target;
  double distance;

  if (_arrival_type) {
    direction_to_target = _ai_char->get_ai_behaviors()->_pursue_obj->_pursue_target.get_pos(_ai_char->_window_render) 
                                                            - _ai_char->_ai_char_np.get_pos(_ai_char->_window_render);
  }
  else {
    direction_to_target = _ai_char->get_ai_behaviors()->_seek_obj->_seek_position
                                                  - _ai_char->_ai_char_np.get_pos(_ai_char->_window_render);
  }
  distance = direction_to_target.length();

  _arrival_direction = direction_to_target;
  _arrival_direction.normalize();

  if (int(distance) == 0) {
    _ai_char->_steering->_steering_force = LVecBase3f(0.0, 0.0, 0.0);
    _ai_char->_steering->_arrival_force = LVecBase3f(0.0, 0.0, 0.0);

    if (_ai_char->_steering->_seek_obj != NULL) {
      _ai_char->_steering->turn_off("arrival");
      _ai_char->_steering->turn_on("arrival_activate");
    }
    _arrival_done = true;
    return (LVecBase3f(0.0, 0.0, 0.0));
  }
  else {
    _arrival_done = false;
  }

  double u = _ai_char->get_velocity().length();
  LVecBase3f desired_force = ((u * u) / (2 * distance)) * _ai_char->get_mass();

  if (_ai_char->_steering->_seek_obj != NULL) {
    return (desired_force);
  }

  if (_ai_char->_steering->_pursue_obj != NULL) {

    if (distance > _arrival_distance) {
      _ai_char->_steering->turn_off("arrival");
      _ai_char->_steering->turn_on("arrival_activate");
      _ai_char->_steering->resume_ai("pursue");
    }

    return (desired_force);
  }

  cout << "Arrival works only with seek and pursue\n";
  return (LVecBase3f(0.0, 0.0, 0.0));
}

////////////////////////////////////////////////////////////////////
//     Function: arrival_activate
//  Description: This function checks for whether the target is
//               within the arrival distance. When this is true,
//               it calls the do_arrival function and sets the
//               arrival direction.
//               This function is not to be used by the user.
////////////////////////////////////////////////////////////////////
void Arrival::arrival_activate() {
  LVecBase3f dirn;
  if (_arrival_type) {
    dirn = (_ai_char->_ai_char_np.get_pos(_ai_char->_window_render) - _ai_char->get_ai_behaviors()->_pursue_obj->_pursue_target.get_pos(_ai_char->_window_render));
  }
  else {
    dirn = (_ai_char->_ai_char_np.get_pos(_ai_char->_window_render) - _ai_char->get_ai_behaviors()->_seek_obj->_seek_position);
  }
  double distance = dirn.length();

  if (distance < _arrival_distance && _ai_char->_steering->_steering_force.length() > 0) {
    _ai_char->_steering->turn_off("arrival_activate");
    _ai_char->_steering->turn_on("arrival");

    if (_ai_char->_steering->is_on(_ai_char->_steering->BT_seek)) {
      _ai_char->_steering->turn_off("seek");
    }

    if (_ai_char->_steering->is_on(_ai_char->_steering->BT_pursue)) {
      _ai_char->_steering->pause_ai("pursue");
    }
  }
}

/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Code by Andrew Tridgell and Siddharth Bharat Purohit
 */

#pragma once

#include "AP_RCProtocol.h"

class AP_RCProtocol_Backend {
    friend class AP_RCProtcol;

public:
    AP_RCProtocol_Backend(AP_RCProtocol &_frontend);
    virtual ~AP_RCProtocol_Backend() {}
    virtual void process_pulse(uint32_t width_s0, uint32_t width_s1) {}
    virtual void process_byte(uint8_t byte, uint32_t baudrate) {}
    uint16_t read(uint8_t chan);
    bool new_input();
    uint8_t num_channels();

    // support for receivers that have FC initiated bind support
    virtual void start_bind(void) {}

    // allow for backends that need regular polling
    virtual void update(void) {}
    enum {
        PARSE_TYPE_SIGREAD,
        PARSE_TYPE_SERIAL
    };

    // get number of frames, ignoring failsafe
    uint32_t get_rc_frame_count(void) const {
        return rc_frame_count;
    }

    // get number of frames, honoring failsafe
    uint32_t get_rc_input_count(void) const {
        return rc_input_count;
    }

    // get RSSI
    int16_t get_RSSI(void) const {
        return rssi;
    }
    
protected:
    void add_input(uint8_t num_channels, uint16_t *values, bool in_failsafe, int16_t rssi=-1);

private:
    AP_RCProtocol &frontend;
    uint32_t rc_input_count;
    uint32_t last_rc_input_count;
    uint32_t rc_frame_count;

    uint16_t _pwm_values[MAX_RCIN_CHANNELS];
    uint8_t  _num_channels;
    int16_t rssi = -1;
};

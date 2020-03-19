#include <AP_HAL/AP_HAL.h>
#include <AP_Common/AP_Common.h>
#include <AP_Math/AP_Math.h>
#include "AP_BattMonitor.h"
#include "AP_BattMonitor_Analog.h"
#include <GCS_MAVLink/GCS.h>

extern const AP_HAL::HAL& hal;

extern volatile int16_t exline1;

static AP_HAL::AnalogSource *last_vsrc;

/// Constructor
AP_BattMonitor_Analog::AP_BattMonitor_Analog(AP_BattMonitor &mon,
                                             AP_BattMonitor::BattMonitor_State &mon_state,
                                             AP_BattMonitor_Params &params) :
    AP_BattMonitor_Backend(mon, mon_state, params)
{
    last_vsrc = _volt_pin_analog_source = hal.analogin->channel(_params._volt_pin);
    _curr_pin_analog_source = hal.analogin->channel(_params._curr_pin);

    // always healthy
    _state.healthy = true;
}

// read - read the voltage and current
void
AP_BattMonitor_Analog::read()
{
    exline1 = __LINE__;
    // this copes with changing the pin at runtime
#if 0
    static bool warning_sent = false;
    if (AP_HAL::millis() > 20000 && !warning_sent) {
        gcs().send_text(MAV_SEVERITY_NOTICE, "Crashing in 5 seconds");
        warning_sent = true;
    }
    if (AP_HAL::millis() > 25000) {
        _volt_pin_analog_source = 0x0;
    }
#endif

    if (last_vsrc != _volt_pin_analog_source) {
        gcs().send_text(MAV_SEVERITY_NOTICE, "VSRC %p %p", _volt_pin_analog_source, last_vsrc);
        return;
    } else {
        _volt_pin_analog_source->set_pin(_params._volt_pin);
    }

    exline1 = __LINE__;
    // get voltage
    _state.voltage = _volt_pin_analog_source->voltage_average() * _params._volt_multiplier;

    exline1 = __LINE__;
    // read current
    if (has_current()) {
        exline1 = __LINE__;
        // calculate time since last current read
        uint32_t tnow = AP_HAL::micros();
        float dt = tnow - _state.last_time_micros;

        exline1 = __LINE__;
        // this copes with changing the pin at runtime
        _curr_pin_analog_source->set_pin(_params._curr_pin);

        exline1 = __LINE__;
        // read current
        _state.current_amps = (_curr_pin_analog_source->voltage_average()-_params._curr_amp_offset)*_params._curr_amp_per_volt;

        exline1 = __LINE__;
        // update total current drawn since startup
        if (_state.last_time_micros != 0 && dt < 2000000.0f) {
            // .0002778 is 1/3600 (conversion to hours)
            exline1 = __LINE__;
            float mah = _state.current_amps * dt * 0.0000002778f;
            _state.consumed_mah += mah;
            _state.consumed_wh  += 0.001f * mah * _state.voltage;
        }

        exline1 = __LINE__;
        // record time
        _state.last_time_micros = tnow;
    }
    exline1 = __LINE__;
}

/// return true if battery provides current info
bool AP_BattMonitor_Analog::has_current() const
{
    return (_params.type() == AP_BattMonitor_Params::BattMonitor_TYPE_ANALOG_VOLTAGE_AND_CURRENT);
}

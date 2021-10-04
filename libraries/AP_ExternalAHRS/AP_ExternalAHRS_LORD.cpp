/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
  suppport for serial connected AHRS systems
 */

#define ALLOW_DOUBLE_MATH_FUNCTIONS

#include "AP_ExternalAHRS_LORD.h"
#include <AP_Baro/AP_Baro.h>
#include <AP_Compass/AP_Compass.h>
#include <AP_GPS/AP_GPS.h>
#include <AP_InertialSensor/AP_InertialSensor.h>
#include <GCS_MAVLink/GCS.h>
#include <AP_Logger/AP_Logger.h>
#include <AP_Common/NMEA.h>
#include <AP_HAL/utility/sparse-endian.h>

#if HAL_EXTERNAL_AHRS_ENABLED

extern const AP_HAL::HAL &hal;

AP_ExternalAHRS_LORD::AP_ExternalAHRS_LORD(AP_ExternalAHRS *_frontend,
    AP_ExternalAHRS::state_t &_state): AP_ExternalAHRS_backend(_frontend, _state) {
    auto &sm = AP::serialmanager();
    uart = sm.find_serial(AP_SerialManager::SerialProtocol_AHRS, 0);

    baudrate = sm.find_baudrate(AP_SerialManager::SerialProtocol_AHRS, 0);
    port_num = sm.find_portnum(AP_SerialManager::SerialProtocol_AHRS, 0);

    if (!uart) {
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "ExternalAHRS no UART");
        return;
    }

    if (!hal.scheduler -> thread_create(FUNCTOR_BIND_MEMBER(&AP_ExternalAHRS_LORD::update_thread, void), "AHRS", 2048, AP_HAL::Scheduler::PRIORITY_SPI, 0)) {
        AP_HAL::panic("Failed to start ExternalAHRS update thread");
    }

    hal.scheduler -> delay(5000);
    GCS_SEND_TEXT(MAV_SEVERITY_INFO, "LORD ExternalAHRS initialised");
}

void AP_ExternalAHRS_LORD::update_thread(void) {
    if (!portOpened) {
        portOpened = true;
        uart -> begin(baudrate);
        // send_config();
    }

    while (true) {
        build_packet();
        hal.scheduler -> delay(1);
    }
}

// Builds packets by looking at each individual byte, once a full packet has been read in it checks the checksum then handles the packet.
void AP_ExternalAHRS_LORD::build_packet() {
    uint64_t nbytes = uart -> available();
    while (nbytes--> 0) {
        int16_t b = uart -> read();

        if (b < 0)
            // No byte to handle
            break;

        switch (message_in.state) {
            default:
            case ParseState::WaitingFor_SyncOne:
                if (b == SYNC_ONE) {
                    message_in.packet.header[0] = b;
                    message_in.state = ParseState::WaitingFor_SyncTwo;
                }
                break;
            case ParseState::WaitingFor_SyncTwo:
                if (b == SYNC_TWO) {
                    message_in.packet.header[1] = b;
                    message_in.state = ParseState::WaitingFor_Descriptor;
                } else {
                    message_in.state = ParseState::WaitingFor_SyncOne;
                }
                break;
            case ParseState::WaitingFor_Descriptor:
                message_in.packet.header[2] = b;
                message_in.state = ParseState::WaitingFor_PayloadLength;
                break;
            case ParseState::WaitingFor_PayloadLength:
                message_in.packet.header[3] = b;
                message_in.state = ParseState::WaitingFor_Data;
                break;
            case ParseState::WaitingFor_Data:
                message_in.packet.payload[message_in.index++] = b;
                if (message_in.index >= message_in.packet.header[3]) {
                    message_in.state = ParseState::WaitingFor_Checksum;
                    message_in.index = 0;
                }
                break;
            case ParseState::WaitingFor_Checksum:
                message_in.packet.checksum[message_in.index++] = b;
                if (message_in.index >= 2) {
                    message_in.state = ParseState::WaitingFor_SyncOne;
                    message_in.index = 0;

                    if (valid_packet(message_in.packet)) {
                        handle_packet(message_in.packet);
                    }
                }
                break;
        }
    }
}

// returns true if the fletcher checksum for the packet is valid, else false.
bool AP_ExternalAHRS_LORD::valid_packet(LORD_Packet & packet) {
    uint8_t checksumByte1 = 0;
    uint8_t checksumByte2 = 0;

    for (int i = 0; i < 4; i++) {
        checksumByte1 += packet.header[i];
        checksumByte2 += checksumByte1;
    }

    for (int i = 0; i < packet.header[3]; i++) {
        checksumByte1 += packet.payload[i];
        checksumByte2 += checksumByte1;
    }

    return packet.checksum[0] == checksumByte1 && packet.checksum[1] == checksumByte2;
}

// Calls the correct functions based on the packet descriptor of the packet
void AP_ExternalAHRS_LORD::handle_packet(LORD_Packet& packet) {
    switch ((DescriptorSet) packet.header[2]) {
        case DescriptorSet::IMUData:
            handle_imu(packet);
            post_imu();
            break;
        case DescriptorSet::GNSSData:
            handle_gnss(packet);
            // post_gnss();
            break;
        case DescriptorSet::EstimationData:
            handle_filter(packet);
            post_filter();
            break;
        case DescriptorSet::BaseCommand:
        case DescriptorSet::DMCommand:
        case DescriptorSet::SystemCommand:
            break;
    }
}

// Collects data from an imu packet into `imu_data`
void AP_ExternalAHRS_LORD::handle_imu(LORD_Packet& packet) {
    last_ins_pkt = AP_HAL::millis();

    for (uint8_t i = 0; i < packet.header[3]; i += packet.payload[i]) {
        switch (packet.payload[i+1]) {
            // Scaled Ambient Pressure
            case 0x17: {
                imu_data.pressure = extract_float(packet.payload, i+2) * 100; // Convert millibar to pascals
                break;
            }
            // Scaled Magnetometer Vector
            case 0x06: {
                imu_data.mag = populate_vector(packet.payload, i+2) * 1000; // Convert gauss to radians
                break;
            }
            // Scaled Accelerometer Vector
            case 0x04: {
                imu_data.accel = populate_vector(packet.payload, i + 2) * 9.8; // Convert g's to m/s^2
                break;
            }
            // Scaled Gyro Vector
            case 0x05: {
                imu_data.gyro = populate_vector(packet.payload, i+2);
                break;
            }
            // CF Quaternion
            case 0x0A: {
                imu_data.quat = populate_quaternion(packet.payload, i+2);
                break;
            }
        }
    }
}

// Posts data from an imu packet to `state` and `handle_external` methods
void AP_ExternalAHRS_LORD::post_imu() const {
    {
        WITH_SEMAPHORE(state.sem);
        state.accel = imu_data.accel;
        state.gyro = imu_data.gyro;
        state.quat = imu_data.quat;
        state.have_quaternion = true;
    }

    {
        AP_ExternalAHRS::ins_data_message_t ins;
        ins.accel = imu_data.accel;
        ins.gyro = imu_data.gyro;
        ins.temperature = -300;
        AP::ins().handle_external(ins);
    }

    {
        AP_ExternalAHRS::mag_data_message_t mag;
        mag.field = imu_data.mag;
        AP::compass().handle_external(mag);
    }

    {
        AP_ExternalAHRS::baro_data_message_t baro;
        baro.instance = 0;
        baro.pressure_pa = imu_data.pressure;
        // setting temp to 25 effectively disables barometer temperature calibrations - these are already performed by lord
        baro.temperature = 25;
        AP::baro().handle_external(baro);
    }
}

// Collects data from an gnss packet into `gnss_data`
void AP_ExternalAHRS_LORD::handle_gnss(LORD_Packet &packet) {
    last_gps_pkt = AP_HAL::millis();

    for (uint8_t i = 0; i < packet.header[3]; i += packet.payload[i]) {
        switch (packet.payload[i+1]) {
            // GPS Time
            case 0x09: {
                gnss_data.tow_ms = extract_double(packet.payload, i+2) * 1000; // Convert seconds to ms
                gnss_data.week = be16toh_ptr(&packet.payload[i+10]);
                break;
            }
            // GNSS Fix Information
            case 0x0B: {
                switch (packet.payload[i+2]) {
                    case (0x00): {
                        gnss_data.fix_type = GPS_FIX_TYPE_3D_FIX;
                        break;
                    }
                    case (0x01): {
                        gnss_data.fix_type = GPS_FIX_TYPE_2D_FIX;
                        break;
                    }
                    case (0x02):
                    case (0x03): {
                        gnss_data.fix_type = GPS_FIX_TYPE_NO_FIX;
                        break;
                    }
                    default:
                    case (0x04): {
                        gnss_data.fix_type = GPS_FIX_TYPE_NO_GPS;
                        break;
                    }
                }

                gnss_data.satellites = packet.payload[i+3];
                break;
            }
            // LLH Position
            case 0x03: {
                gnss_data.lat = extract_double(packet.payload, i+2) * 1.0e7; // Decimal degrees to degrees
                gnss_data.lon = extract_double(packet.payload, i+10) * 1.0e7;
                gnss_data.msl_altitude = extract_double(packet.payload, i+26) * 1.0e2; // Meters to cm
                gnss_data.horizontal_position_accuracy = extract_float(packet.payload, i+34);
                gnss_data.vertical_position_accuracy = extract_float(packet.payload, i+38);
                break;
            }
            // DOP Data
            case 0x07: {
                gnss_data.hdop = extract_float(packet.payload, i+10);
                gnss_data.vdop = extract_float(packet.payload, i+14);
                break;
            }
            // NED Velocity
            case 0x05: {
                gnss_data.ned_velocity_north = extract_float(packet.payload, i+2);
                gnss_data.ned_velocity_east = extract_float(packet.payload, i+6);
                gnss_data.ned_velocity_down = extract_float(packet.payload, i+10);
                gnss_data.speed_accuracy = extract_float(packet.payload, i+26);
                break;
            }
        }
    }
}

// Posts data from a gnss packet to `state` and `handle_external` methods
void AP_ExternalAHRS_LORD::post_gnss() const {
    AP_ExternalAHRS::gps_data_message_t gps;

    gps.gps_week = gnss_data.week;
    gps.ms_tow = gnss_data.tow_ms;
    gps.fix_type = gnss_data.fix_type;
    gps.satellites_in_view = gnss_data.satellites;

    gps.horizontal_pos_accuracy = gnss_data.horizontal_position_accuracy;
    gps.vertical_pos_accuracy = gnss_data.vertical_position_accuracy;

    gps.latitude = gnss_data.lat;
    gps.longitude = gnss_data.lon;
    gps.msl_altitude = gnss_data.msl_altitude;

    gps.ned_vel_north = gnss_data.ned_velocity_north;
    gps.ned_vel_east = gnss_data.ned_velocity_east;
    gps.ned_vel_down = gnss_data.ned_velocity_down;
    gps.horizontal_vel_accuracy = gnss_data.speed_accuracy;

    gps.hdop = gnss_data.hdop;
    gps.vdop = gnss_data.vdop;

    if (gps.fix_type >= 3 && !state.have_origin) {
        WITH_SEMAPHORE(state.sem);
        state.origin = Location{int32_t(gnss_data.lat),
                                int32_t(gnss_data.lon),
                                int32_t(gnss_data.msl_altitude),
                                Location::AltFrame::ABSOLUTE};
        state.have_origin = true;
    }

    AP::gps().handle_external(gps);
}

void AP_ExternalAHRS_LORD::handle_filter(LORD_Packet &packet) {
        last_filter_pkt = AP_HAL::millis();

        for (uint8_t i = 0; i < packet.header[3]; i += packet.payload[i]) {
            switch (packet.payload[i+1]) {
                // GPS Timestamp
                case 0x11: {
                    filter_data.tow_ms = extract_double(packet.payload, i+2) * 1000; // Convert seconds to ms
                    filter_data.week = be16toh_ptr(&packet.payload[i+10]);
                    break;
                }
                // LLH Position
                case 0x01: {
                    filter_data.lat = extract_double(packet.payload, i+2) * 1.0e7; // Decimal degrees to degrees
                    filter_data.lon = extract_double(packet.payload, i+10) * 1.0e7;
                    filter_data.msl_altitude = extract_double(packet.payload, i+26) * 1.0e2; // Meters to cm
                    break;
                }
                // NED Velocity
                case 0x02: {
                    filter_data.ned_velocity_north = extract_float(packet.payload, i+2);
                    filter_data.ned_velocity_east = extract_float(packet.payload, i+6);
                    filter_data.ned_velocity_down = extract_float(packet.payload, i+10);
                    break;
                }
                // Filter Status
                case 0x10: {
                    filter_status.state = be16toh_ptr(&packet.payload[i+2]);
                    filter_status.mode = be16toh_ptr(&packet.payload[i+4]);
                    filter_status.flags = be16toh_ptr(&packet.payload[i+6]);
                    break;
                }
            }
        }
}

void AP_ExternalAHRS_LORD::post_filter() const {
    AP_ExternalAHRS::gps_data_message_t gps;

    gps.gps_week = filter_data.week;
    gps.ms_tow = filter_data.tow_ms;
    gps.fix_type = gnss_data.fix_type;
    gps.satellites_in_view = gnss_data.satellites;

    gps.horizontal_pos_accuracy = gnss_data.horizontal_position_accuracy;
    gps.vertical_pos_accuracy = gnss_data.vertical_position_accuracy;

    gps.latitude = filter_data.lat;
    gps.longitude = filter_data.lon;
    gps.msl_altitude = gnss_data.msl_altitude;

    gps.ned_vel_north = filter_data.ned_velocity_north;
    gps.ned_vel_east = filter_data.ned_velocity_east;
    gps.ned_vel_down = filter_data.ned_velocity_down;
    gps.horizontal_vel_accuracy = gnss_data.speed_accuracy;

    gps.hdop = gnss_data.hdop;
    gps.vdop = gnss_data.vdop;

    if (gps.fix_type >= 3 && !state.have_origin) {
        WITH_SEMAPHORE(state.sem);
        state.origin = Location{int32_t(gnss_data.lat),
                                int32_t(gnss_data.lon),
                                int32_t(gnss_data.msl_altitude),
                                Location::AltFrame::ABSOLUTE};
        state.have_origin = true;
    }

    AP::gps().handle_external(gps);
}

int8_t AP_ExternalAHRS_LORD::get_port(void) const {
    if (!uart)
        return -1;
    return port_num;
};

bool AP_ExternalAHRS_LORD::healthy(void) const {
    uint32_t now = AP_HAL::millis();
    return (now - last_ins_pkt < 40 && now - last_gps_pkt < 500 && now - last_filter_pkt < 500);
}

bool AP_ExternalAHRS_LORD::initialised(void) const {
    return last_ins_pkt != 0 && last_gps_pkt != 0 && last_filter_pkt != 0;
}

bool AP_ExternalAHRS_LORD::pre_arm_check(char *failure_msg, uint8_t failure_msg_len) const {
    if (!healthy()) {
        hal.util->snprintf(failure_msg, failure_msg_len, "LORD unhealthy");
        return false;
    }
    if (gnss_data.fix_type < 3) {
        hal.util->snprintf(failure_msg, failure_msg_len, "LORD no GPS lock");
        return false;
    }

    return true;
}

void AP_ExternalAHRS_LORD::get_filter_status(nav_filter_status &status) const {
    memset(&status, 0, sizeof(status));
    if (last_ins_pkt!=0 && last_gps_pkt!=0) {
        status.flags.initalized = 1;
    }
    if (healthy() && last_ins_packet!=0) {
        status.flags.attitude = 1;
        status.flags.vert_vel = 1;
        status.flags.vert_pos = 1;

        if (fix_type >= 3) {
            status.flags.horiz_vel = 1;
            status.flags.horiz_pos_rel = 1;
            status.flags.horiz_pos_abs = 1;
            status.flags.pred_horiz_pos_rel = 1;
            status.flags.pred_horiz_pos_abs = 1;
            status.flags.using_gps = 1;
        }
    }
}

void AP_ExternalAHRS_LORD::send_status_report(mavlink_channel_t chan) const {
    // prepare flags
    uint16_t flags = 0;
    nav_filter_status filterStatus;
    get_filter_status(filterStatus);
    if (filterStatus.flags.attitude) {
        flags |= EKF_ATTITUDE;
    }
    if (filterStatus.flags.horiz_vel) {
        flags |= EKF_VELOCITY_HORIZ;
    }
    if (filterStatus.flags.vert_vel) {
        flags |= EKF_VELOCITY_VERT;
    }
    if (filterStatus.flags.horiz_pos_rel) {
        flags |= EKF_POS_HORIZ_REL;
    }
    if (filterStatus.flags.horiz_pos_abs) {
        flags |= EKF_POS_HORIZ_ABS;
    }
    if (filterStatus.flags.vert_pos) {
        flags |= EKF_POS_VERT_ABS;
    }
    if (filterStatus.flags.terrain_alt) {
        flags |= EKF_POS_VERT_AGL;
    }
    if (filterStatus.flags.const_pos_mode) {
        flags |= EKF_CONST_POS_MODE;
    }
    if (filterStatus.flags.pred_horiz_pos_rel) {
        flags |= EKF_PRED_POS_HORIZ_REL;
    }
    if (filterStatus.flags.pred_horiz_pos_abs) {
        flags |= EKF_PRED_POS_HORIZ_ABS;
    }
    if (!filterStatus.flags.initalized) {
        flags |= EKF_UNINITIALIZED;
    }

    // send message
    const float vel_gate = 5; // represents hz value data is posted at
    const float pos_gate = 5; // represents hz value data is posted at
    const float hgt_gate = 5; // represents hz value data is posted at
    const float mag_var = 0; //we may need to change this to be like the other gates, set to 0 because mag is ignored by the ins filter in vectornav
    mavlink_msg_ekf_status_report_send(chan, flags,
                                       gnss_data.speed_accuracy/vel_gate, gnss_data.horizontal_position_accuracy/pos_gate, gnss_data.vertical_position_accuracy/hgt_gate,
                                       mag_var, 0, 0);

}

Vector3f AP_ExternalAHRS_LORD::populate_vector(uint8_t *data, uint8_t offset) const {
    Vector3f vector;

    vector.x = extract_float(data, offset);
    vector.y = extract_float(data, offset+4);
    vector.z = extract_float(data, offset+8);

    return vector;
}

Quaternion AP_ExternalAHRS_LORD::populate_quaternion(uint8_t *data, uint8_t offset) const {
    Quaternion quat;

    quat.q1 = extract_float(data, offset);
    quat.q2 = extract_float(data, offset+4);
    quat.q3 = extract_float(data, offset+8);
    quat.q4 = extract_float(data, offset+12);

    return quat;
}

float AP_ExternalAHRS_LORD::extract_float(uint8_t *data, uint8_t offset) const {
    uint32_t tmp = be32toh_ptr(&data[offset]);

    return *reinterpret_cast<float*>(&tmp);
}    void send_config();

double AP_ExternalAHRS_LORD::extract_double(uint8_t *data, uint8_t offset) const {
    uint64_t tmp = be64toh_ptr(&data[offset]);

    return *reinterpret_cast<double*>(&tmp);
}

#endif // HAL_EXTERNAL_AHRS_ENABLED
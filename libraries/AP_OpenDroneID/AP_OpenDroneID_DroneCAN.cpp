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
 */
/*
  DroneCAN support for OpenDroneID
 */

#include "AP_OpenDroneID.h"

#if AP_OPENDRONEID_ENABLED

#include <AP_UAVCAN/AP_UAVCAN.h>
#include <dronecan/remoteid/Location.hpp>
#include <dronecan/remoteid/BasicID.hpp>
#include <dronecan/remoteid/SelfID.hpp>
#include <dronecan/remoteid/OperatorID.hpp>
#include <dronecan/remoteid/System.hpp>

#if HAL_MAX_CAN_PROTOCOL_DRIVERS
static uavcan::Publisher<dronecan::remoteid::Location>* dc_location[HAL_MAX_CAN_PROTOCOL_DRIVERS];
static uavcan::Publisher<dronecan::remoteid::BasicID>* dc_basic_id[HAL_MAX_CAN_PROTOCOL_DRIVERS];
static uavcan::Publisher<dronecan::remoteid::SelfID>* dc_self_id[HAL_MAX_CAN_PROTOCOL_DRIVERS];
static uavcan::Publisher<dronecan::remoteid::System>* dc_system[HAL_MAX_CAN_PROTOCOL_DRIVERS];
static uavcan::Publisher<dronecan::remoteid::OperatorID>* dc_operator_id[HAL_MAX_CAN_PROTOCOL_DRIVERS];

void AP_OpenDroneID::dronecan_init(AP_UAVCAN *uavcan)
{
    const uint8_t driver_index = uavcan->get_driver_index();
    const uint8_t driver_mask = 1U<<driver_index;
    if (dronecan_done_init & driver_mask) {
        // already initialised
        return;
    }

    dc_location[driver_index] = new uavcan::Publisher<dronecan::remoteid::Location>(*uavcan->get_node());
    if (dc_location[driver_index] == nullptr) {
        goto alloc_failed;
    }
    dc_location[driver_index]->setTxTimeout(uavcan::MonotonicDuration::fromMSec(20));
    dc_location[driver_index]->setPriority(uavcan::TransferPriority::OneHigherThanLowest);

    dc_basic_id[driver_index] = new uavcan::Publisher<dronecan::remoteid::BasicID>(*uavcan->get_node());
    if (dc_basic_id[driver_index] == nullptr) {
        goto alloc_failed;
    }
    dc_basic_id[driver_index]->setTxTimeout(uavcan::MonotonicDuration::fromMSec(20));
    dc_basic_id[driver_index]->setPriority(uavcan::TransferPriority::OneHigherThanLowest);

    dc_self_id[driver_index] = new uavcan::Publisher<dronecan::remoteid::SelfID>(*uavcan->get_node());
    if (dc_self_id[driver_index] == nullptr) {
        goto alloc_failed;
    }
    dc_self_id[driver_index]->setTxTimeout(uavcan::MonotonicDuration::fromMSec(20));
    dc_self_id[driver_index]->setPriority(uavcan::TransferPriority::OneHigherThanLowest);

    dc_system[driver_index] = new uavcan::Publisher<dronecan::remoteid::System>(*uavcan->get_node());
    if (dc_system[driver_index] == nullptr) {
        goto alloc_failed;
    }
    dc_system[driver_index]->setTxTimeout(uavcan::MonotonicDuration::fromMSec(20));
    dc_system[driver_index]->setPriority(uavcan::TransferPriority::OneHigherThanLowest);

    dc_operator_id[driver_index] = new uavcan::Publisher<dronecan::remoteid::OperatorID>(*uavcan->get_node());
    if (dc_operator_id[driver_index] == nullptr) {
        goto alloc_failed;
    }
    dc_operator_id[driver_index]->setTxTimeout(uavcan::MonotonicDuration::fromMSec(20));
    dc_operator_id[driver_index]->setPriority(uavcan::TransferPriority::OneHigherThanLowest);
    
    dronecan_done_init |= driver_mask;
    return;

alloc_failed:
    dronecan_init_failed |= driver_mask;
    GCS_SEND_TEXT(MAV_SEVERITY_NOTICE, "OpenDroneID DroneCAN alloc failed");
}

/*
  send pending DroneCAN OpenDroneID packets
 */
void AP_OpenDroneID::dronecan_send(AP_UAVCAN *uavcan)
{
    if (!option_set(Options::EnableDroneCAN)) {
        return;
    }

    const uint8_t driver_index = uavcan->get_driver_index();
    const uint8_t driver_mask = 1U<<driver_index;

    dronecan_init(uavcan);

    if (dronecan_init_failed & driver_mask) {
        return;
    }

    if (need_send_location & driver_mask) {
        WITH_SEMAPHORE(_sem);
        dronecan_send_location(uavcan);
        need_send_location &= ~driver_mask;
    }
    if (need_send_basic_id & driver_mask) {
        WITH_SEMAPHORE(_sem);
        dronecan_send_basic_id(uavcan);
        need_send_basic_id &= ~driver_mask;
    }
    if (need_send_system & driver_mask) {
        WITH_SEMAPHORE(_sem);
        dronecan_send_system(uavcan);
        need_send_system &= ~driver_mask;
    }
    if (need_send_self_id & driver_mask) {
        WITH_SEMAPHORE(_sem);
        dronecan_send_self_id(uavcan);
        need_send_self_id &= ~driver_mask;
    }
    if (need_send_operator_id & driver_mask) {
        WITH_SEMAPHORE(_sem);
        dronecan_send_operator_id(uavcan);
        need_send_operator_id &= ~driver_mask;
    }
}

#define ODID_COPY(name) msg.name = pkt.name
#define ODID_COPY_STR(name) do { for (uint8_t i = 0; i<sizeof(pkt.name) && pkt.name[i]; i++) msg.name.push_back(pkt.name[i]); } while(0)


void AP_OpenDroneID::dronecan_send_location(AP_UAVCAN *uavcan)
{
    dronecan::remoteid::Location msg;
    const auto &pkt = pkt_location;
    ODID_COPY_STR(id_or_mac);
    ODID_COPY(status);
    ODID_COPY(direction);
    ODID_COPY(speed_horizontal);
    ODID_COPY(speed_vertical);
    ODID_COPY(latitude);
    ODID_COPY(longitude);
    ODID_COPY(altitude_barometric);
    ODID_COPY(altitude_geodetic);
    ODID_COPY(height_reference);
    ODID_COPY(height);
    ODID_COPY(horizontal_accuracy);
    ODID_COPY(vertical_accuracy);
    ODID_COPY(barometer_accuracy);
    ODID_COPY(speed_accuracy);
    ODID_COPY(timestamp);
    ODID_COPY(timestamp_accuracy);
    dc_location[uavcan->get_driver_index()]->broadcast(msg);
}

void AP_OpenDroneID::dronecan_send_basic_id(AP_UAVCAN *uavcan)
{
    dronecan::remoteid::BasicID msg;
    const auto &pkt = pkt_basic_id;
    ODID_COPY_STR(id_or_mac);
    ODID_COPY(id_type);
    ODID_COPY(ua_type);
    ODID_COPY_STR(uas_id);
    dc_basic_id[uavcan->get_driver_index()]->broadcast(msg);
}

void AP_OpenDroneID::dronecan_send_system(AP_UAVCAN *uavcan)
{
    dronecan::remoteid::System msg {};
    const auto &pkt = pkt_system;
    ODID_COPY_STR(id_or_mac);
    ODID_COPY(operator_location_type);
    ODID_COPY(classification_type);
    ODID_COPY(operator_latitude);
    ODID_COPY(operator_longitude);
    ODID_COPY(area_count);
    ODID_COPY(area_radius);
    ODID_COPY(area_ceiling);
    ODID_COPY(area_floor);
    ODID_COPY(category_eu);
    ODID_COPY(class_eu);
    ODID_COPY(operator_altitude_geo);
    ODID_COPY(timestamp);
    dc_system[uavcan->get_driver_index()]->broadcast(msg);
}

void AP_OpenDroneID::dronecan_send_self_id(AP_UAVCAN *uavcan)
{
    dronecan::remoteid::SelfID msg {};
    const auto &pkt = pkt_self_id;
    ODID_COPY_STR(id_or_mac);
    ODID_COPY(description_type);
    ODID_COPY_STR(description);
    dc_self_id[uavcan->get_driver_index()]->broadcast(msg);
}

void AP_OpenDroneID::dronecan_send_operator_id(AP_UAVCAN *uavcan)
{
    dronecan::remoteid::OperatorID msg {};
    const auto &pkt = pkt_operator_id;
    ODID_COPY_STR(id_or_mac);
    ODID_COPY(operator_id_type);
    ODID_COPY_STR(operator_id);
    dc_operator_id[uavcan->get_driver_index()]->broadcast(msg);
}

#endif
#endif // AP_OPENDRONEID_ENABLED

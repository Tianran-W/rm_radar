#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "driver/serial/serial.h"
#include "protocol/referee_system.h"
#include "robot/robot.h"

namespace radar {

/**
 * @brief
 */
class RefereeCommunicator {
   public:
    /**
     * @brief Constructor for the RefereeCommunicator class.
     *
     * @param serial_addr The name of the serial device (e.g., "/dev/ttyUSB0").
     */
    RefereeCommunicator(std::string_view serial_addr);

    /**
     * @brief Try to reopen serial port after init failed.
     *
     * @return Whether the reconnection success or not
     */
    bool reconnect();

    /**
     * @brief Send the position data of the robot to the referee system.
     *
     * @param robots Robot objects ready to be sent
     */
    void sendMapRobot(const std::span<const Robot> robots);

    /**
     * @brief Receive data sent by the referee system and update internal
     * variables.
     */
    void update();

   private:
    enum class DecodeStatus {
        Free,
        Length,
        CRC16,
    };

    // Disable default constructor
    RefereeCommunicator() = delete;

    /**
     * @brief Encode and send datagram with specific command format
     *
     * @param cmd Type of command to be encoded
     * @param data Raw data that contains bytes of command type data
     *
     * @return Flag indicating if send successfully
     */
    bool encode(protocol::CommandCode cmd,
                std::vector<std::byte>&& data) noexcept;

    /**
     * @brief Receive data sent by the referee system and update internal
     * variables.
     *
     * @param id Subcommand type
     * @param receiver Robot to send datagram to
     * @param data Raw data that contains bytes of command type data
     *
     * @return Flag indicating if send successfully
     */
    bool encode(protocol::SubContentId id, protocol::Id receiver,
                std::vector<std::byte>&& data) noexcept;

    /**
     * @brief Decode the datagram received from the referee system.
     *
     * @return Flag indicating if send successfully
     */
    bool decode() noexcept;

    /**
     * @brief Update the corresponding member variables according to the data
     * and command code.
     *
     * @param data raw data
     * @param command_id command type indicating which field is used
     *
     * @return Flag indicating if send successfully
     */
    bool fetchData(std::span<std::byte> data,
                   protocol::CommandCode command_id) noexcept;

    /**
     * @brief Judge whether the robot is an enemy.
     *
     * @param label label of robot to be judged
     *
     * @return Flag indicating if robot is enemy
     */
    bool isEnemy(Robot::Label label) noexcept;

    /**
     * @brief Append 8 bit of CRC to data
     *
     * @param data if raw data has length of n, then length of data should be
     * n+1 for append CRC bit
     */
    static void appendCRC8(std::span<std::byte> data) noexcept;

    /**
     * @brief Append 16 bit of CRC to data
     *
     * @param data if raw data has length of n, then length of data should be
     * n+2 for append CRC bit
     */
    static void appendCRC16(std::span<std::byte> data) noexcept;

    /**
     * @brief Verify 8 bit CRC of data
     *
     * @param data if data has length of n, then n-1 byte from start will be
     * used as raw data
     *
     * @return Flag indicating if CRC8 verified
     */
    static bool verifyCRC8(std::span<const std::byte> data) noexcept;

    /**
     * @brief Verify 16 bit CRC of data
     *
     * @param data if data has length of n, then n-2 byte from start will be
     * used as raw data
     *
     * @return Flag indicating if CRC16 verified
     */
    static bool verifyCRC16(std::span<const std::byte> data) noexcept;

    /// Serial object for handling serial port operations
    std::unique_ptr<serial::Serial> serial_;

    /// Flag indicating if radar is connected to serial port
    std::atomic_bool is_connected_ = false;

    /// Mutex to ensure thread safety for communication
    std::shared_mutex comm_mutex_;

    /// Status of global game
    std::shared_ptr<protocol::game_status_t> game_status_;

    /// Status indicating the winner
    std::shared_ptr<protocol::game_result_t> game_result_;

    /// HP of all robots
    std::shared_ptr<protocol::game_robot_HP_t> robot_health_point_;

    /// Site event data
    std::shared_ptr<protocol::event_data_t> event_data_;

    /// Action identifier data of the Official Projectile Supplier
    std::shared_ptr<protocol::ext_supply_projectile_action_t>
        projectile_action_;

    /// Referee warning data
    std::shared_ptr<protocol::referee_warning_t> referee_warning_;

    /// Dart launching data
    std::shared_ptr<protocol::dart_info_t> dart_info_;

    /// Robot performance system data of radar
    std::shared_ptr<protocol::robot_status_t> radar_status_;

    /// Radar-marked progress data
    std::shared_ptr<protocol::radar_mark_data_t> radar_mark_data_;

    /// Decision-making data of Radar
    std::shared_ptr<protocol::radar_info_t> radar_info_;

    /// Robot interaction data from sentry
    std::shared_ptr<protocol::robot_interaction_data_t> sentry_data_;

    /// Timestamp of last receiving data
    std::chrono::high_resolution_clock::time_point last_receive_timestamp_;

    /// Buffer for data
    std::vector<std::byte> data_buffer_;
};

}  // namespace radar
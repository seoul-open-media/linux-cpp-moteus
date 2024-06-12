#pragma once

#include "slcm_udp.hpp"
#include "som_linuxcppmoteus.hpp"

namespace som {
class DifferentialJointUdpServoSystem : public UdpServoSystem {
 public:
  DifferentialJointUdpServoSystem(const std::string& udp_host,
                                  const int udp_recv_port,
                                  const int udp_send_port)
      : UdpServoSystem{{{4, 1}, {5, 1}},
                       udp_host,
                       udp_recv_port,
                       udp_send_port,
                       CmdPosRelTo::cmdRECENT,
                       RplPosRelTo::rplABSOLUTE,
                       "../config",
                       "../config",
                       true,
                       RplPosRelTo::rplABSOLUTE} {}

 protected:
  virtual void ExternalCommandGetter(std::atomic_bool* terminated) override {
    std::cout << "Differential joint variant ExternalCommandGetter thread is "
                 "running..."
              << std::endl;

    const auto& maybe_servo_l = Utils::SafeAt(servos_, 4);
    const auto& maybe_servo_r = Utils::SafeAt(servos_, 5);
    if (!maybe_servo_l) {
      std::cout << "Servo ID 4 not ready.  Now terminating." << std::endl;
      return;
    }
    const auto& servo_l = maybe_servo_l.value();
    if (!maybe_servo_r) {
      std::cout << "Servo ID 5 not ready.  Now terminating." << std::endl;
      return;
    }

    const auto& servo_r = maybe_servo_r.value();
    /// Setup UDP socket
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
      std::cout << "Failed to create UDP socket. "
                   "Differential joint variant ExternalCommandGetter thread "
                   "will now terminate."
                << std::endl;
      return;
    }
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(udp_.host.c_str());
    address.sin_port = htons(udp_.recv_port);
    if (bind(udp_socket, (struct sockaddr*)&address, sizeof(address)) < 0) {
      std::cout << "Failed to bind UDP socket.  "
                   "Differential joint variant ExternalCommandGetter thread "
                   "will now terminate."
                << std::endl;
      close(udp_socket);
      return;
    }
    std::cout << "Differential joint variant ExternalCommandGetter thread "
                 "started listening for UDP packets on "
              << udp_.host << ":" << udp_.recv_port << "..." << std::endl;

    /// Listen for UDP packets in an infinite loop
    while (!((*terminated).load())) {
      ::usleep(cycle_period_us_);

      std::map<int, bool> receive_states;  // ID -> (Data received for this ID?)
      for (auto id : ids_) {
        receive_states[id] = false;
      }
      std::map<int, std::map<CmdItem, double>> cmd;

      /// Inner loop until data are received for all IDs
      while (!std::all_of(receive_states.begin(), receive_states.end(),
                          [](const auto& pair) { return pair.second; })) {
        RecvBuf buffer;

        sockaddr_in client_address;
        socklen_t client_address_len = sizeof(client_address);
        ssize_t bytes_received =
            recvfrom(udp_socket, buffer.raw_bytes, sizeof(buffer.raw_bytes), 0,
                     (struct sockaddr*)&client_address, &client_address_len);
        if (bytes_received < 0) {
          std::cout << "UDP receive error!" << std::endl;
          continue;
        }

        int id = static_cast<int>(buffer.cmd.id);
        if (ids_.find(id) == ids_.end()) continue;
        if (receive_states[id]) continue;

        if (Utils::IsLittleEndian()) {
          for (int i = 1; i + 4 <= sizeof(buffer.raw_bytes); i += 4) {
            std::reverse(buffer.raw_bytes + i, buffer.raw_bytes + i + 4);
          }
        }

        cmd[id][CmdItem::POSITION] = static_cast<double>(buffer.cmd.position);
        cmd[id][CmdItem::VELOCITY_LIMIT] =
            static_cast<double>(buffer.cmd.velocity);
        cmd[id][CmdItem::MAXIMUM_TORQUE] =
            static_cast<double>(buffer.cmd.maximum_torque);
        cmd[id][CmdItem::ACCEL_LIMIT] =
            static_cast<double>(buffer.cmd.accel_limit);

        receive_states[id] = true;
      }

      const double target_diff = cmd[4][CmdItem::POSITION];
      const double target_avg = cmd[5][CmdItem::POSITION];
      const double cur_diff = servo_l->GetReply().abs_position;
      const double cur_avg = servo_r->GetReply().abs_position;
      const double target_delta_diff = target_diff - cur_diff;
      const double target_delta_avg = target_avg - cur_avg;

      cmd[4][CmdItem::POSITION] =
          41.0 * 127.0 / 92.0 *
          (target_delta_avg + 145.0 / 127.0 * target_delta_diff);
      cmd[5][CmdItem::POSITION] =
          41.0 * 127.0 / 92.0 *
          (target_delta_avg - 145.0 / 127.0 * target_delta_diff);

      std::cout << "target_diff = " << target_diff << std::endl;
      std::cout << "target_avg = " << target_avg << std::endl;
      std::cout << "cur_diff = " << cur_diff << std::endl;
      std::cout << "cur_avg = " << cur_avg << std::endl;
      std::cout << "target_delta_diff = " << target_delta_diff << std::endl;
      std::cout << "target_delta_avg = " << target_delta_avg << std::endl;
      std::cout << "target_delta_l = " << cmd[4][CmdItem::POSITION]
                << std::endl;
      std::cout << "target_delta_r = " << cmd[5][CmdItem::POSITION]
                << std::endl;
      EmplaceCommand(cmd);
    }

    close(udp_socket);
  }
};
}  // namespace som
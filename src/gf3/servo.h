#pragma once

#include "helpers/helpers.h"

namespace gf3 {

class Servo : public Controller {
 public:
  Servo(const int id, const uint8_t bus,
        const std::shared_ptr<Transport>& transport,  //
        const PmFmt* pm_fmt, const QFmt* q_fmt)
      : id_{id},
        Controller{[&]() {
          Options options;
          options.id = id;
          options.bus = bus;
          options.transport = transport;
          if (pm_fmt) options.position_format = *pm_fmt;
          options.default_query = false;  // Query right before Command,
                                          // not along with Command.
                                          // Make sure to pass format_override
                                          // argument whenever Querying.
          return options;
        }()},
        q_fmt_{q_fmt} {
    const auto maybe_rpl = SetStop(q_fmt_);
    if (maybe_rpl) {
      last_rpl_.abs_position = 0.0;
      SetReply(maybe_rpl->values);
    } else {
      std::cout << "Servo ID " << id << " is NOT responding." << std::endl;
    }
  }

  int GetId() const { return id_; }

  CanFdFrame MakeQuery() {
    return static_cast<Controller*>(this)->MakeQuery(q_fmt_);
  }

  CanFdFrame MakePositionRelativeToRecent(PmCmd cmd) {
    if (std::isnan(cmd.position)) {
      std::cout << "You requested a Recent-relative PmCmd "
                   "with PmCmd::position set to NaN.  "
                   "Leaving PmCmd::position to NaN."
                << std::endl;
    } else if (utils::GetTime() - last_rpl_time_ >= 1.0) {
      std::cout << "You requested a Recent-relative PmCmd "
                   "but more than 1 second has past since last Reply.  "
                   "Setting PmCmd::position to NaN."
                << std::endl;
      cmd.position = NaN;
    } else {
      cmd.position += last_rpl_.position;
    }

    return MakePosition(cmd);
  }

  QRpl GetReplyAux2PositionUncoiled() const {
    auto rpl = last_rpl_;
    rpl.abs_position += aux2_revs_;
    return rpl;
  }

  void SetReply(const QRpl& new_rpl) {
    const auto delta_aux2_pos = new_rpl.abs_position - last_rpl_.abs_position;
    if (delta_aux2_pos > 0.5) {
      aux2_revs_--;
    } else if (delta_aux2_pos < -0.5) {
      aux2_revs_++;
    }

    last_rpl_ = new_rpl;
    last_rpl_time_ = utils::GetTime();
  }

  std::mutex mtx_;

 private:
  const int id_;
  const QFmt* const q_fmt_;
  double last_rpl_time_;
  QRpl last_rpl_;  // Last raw Reply whose aux2 position is coiled.
                   // Should be updated by `SetReply()` to track
                   // aux2 revolutions.
                   // This field necessary in order to compare it
                   // to a new Reply to track aux2 revolutions.
  int aux2_revs_ = 0;
};

}  // namespace gf3

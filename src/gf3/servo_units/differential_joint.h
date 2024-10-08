#pragma once

#include "../globals.h"
#include "../servo.h"

namespace gf3 {
struct DifferentialJointFrameMakers;

// -------------------------------------
// | DifferentialJoints on GF3:        |
// |-----------------------------------|
// | Part            | ServoID  | SUID |
// | Part            | L  | R   |      |
// |-----------------------------------|
// | LeftShoulderXY  | 2  | 3   | 2    |
// | LeftElbow       | 4  | 5   | 4    |
// | RightShoulderXY | 8  | 9   | 8    |
// | RightElbow      | 10 | 11  | 10   |
// | Neck            | 13 | 14  | 13   |
// ---------------------------------------------------------------------
// | DifferentialJoint formula:                                        |
// |-------------------------------------------------------------------|
// | r_avg * Delta(output_avg) = (Delta(rotor_l) + Delta(rotor_r)) / 2 |
// | r_dif * Delta(output_dif) = (Delta(rotor_l) - Delta(rotor_r)) / 2 |
// ---------------------------------------------------------------------
// | Servo | ID  | aux2 | rot+  | aux2rot+ | sgn(r) |
// |------------------------------------------------|
// | Left  | n   | Dif  | -(-x) | -(-z)    | +      |
// | Right | n+1 | Avg  | +(+x) | +(+x)    | +      |
// --------------------------------------------------

class DifferentialJoint {
 public:
  DifferentialJoint(const int& l_id, const int& r_id, const uint8_t& bus,
                    const double& r_dif, const double& r_avg,
                    const double& min_pos_dif, const double& max_pos_dif,
                    const double& min_pos_avg, const double& max_pos_avg)
      : l_{l_id, bus, globals::transport, &globals::pm_fmt, &globals::q_fmt},
        r_{r_id, bus, globals::transport, &globals::pm_fmt, &globals::q_fmt},
        r_dif_{r_dif},
        r_avg_{r_avg},
        min_pos_dif_{min_pos_dif},
        max_pos_dif_{max_pos_dif},
        min_pos_avg_{min_pos_avg},
        max_pos_avg_{max_pos_avg},
        pm_cmd_template_{&globals::pm_cmd_template} {}

  /////////////////
  // Components: //

  Servo l_, r_;

  ////////////////////////////////////////////////
  // DifferentialJoint Command & Reply structs: //

  struct Command {
    friend struct DifferentialJointFrameMakers;
    std::mutex mtx;
    bool loaded;

    enum class Mode : uint8_t { Stop, OutPos, OutVel, Fix } mode = Mode::Stop;

    // Ensure min/max clamp for position, and non-negativeness for velocity,
    // at time of reception from CommandReceivers.
    double pos_dif;
    double vel_dif;
    double pos_avg;
    double vel_avg;
    double max_trq, max_vel, max_acc;  // of rotors.
    bool stop_pending = false;
    bool fix_pending = false;
    inline static const double damp_thr = 0.1;
    inline static const double fix_thr = 0.01;

   private:
    bool fixing = false;
  } cmd_;

  struct Reply {
    std::mutex mtx;

    bool fixing;
    union {
      struct {
        double l, r;
      } delta_pos_rotor;
      struct {
        double l, r;
      } vel_rotor;
    } target;
  } rpl_;

  /////////////////////
  // Configurations: //

  const double r_dif_, r_avg_;
  const double min_pos_dif_, max_pos_dif_, min_pos_avg_, max_pos_avg_;
  const PmCmd* const pm_cmd_template_;

  ////////////////////////////////////////
  // Command serializer & deserializer: //

  friend void to_json(json& j, const DifferentialJoint& dj) {
    j = json{{"suid", dj.l_.GetId()},       //
             {"pos_dif", dj.cmd_.pos_dif},  //
             {"vel_dif", dj.cmd_.vel_dif},  //
             {"pos_avg", dj.cmd_.pos_avg},  //
             {"vel_avg", dj.cmd_.vel_avg},  //
             {"max_trq", dj.cmd_.max_trq},  //
             {"max_vel", dj.cmd_.max_vel},  //
             {"max_acc", dj.cmd_.max_acc}};
  }

  friend void from_json(const json& j, DifferentialJoint& dj) {
    j.at("pos_dif").get_to(dj.cmd_.pos_dif);
    j.at("vel_dif").get_to(dj.cmd_.vel_dif);
    j.at("pos_avg").get_to(dj.cmd_.pos_avg);
    j.at("vel_avg").get_to(dj.cmd_.vel_avg);
    j.at("max_trq").get_to(dj.cmd_.max_trq);
    j.at("max_vel").get_to(dj.cmd_.max_vel);
    j.at("max_acc").get_to(dj.cmd_.max_acc);

    // For compatibility with poses saved before changing
    // "where to clamp at" policy.
    dj.cmd_.pos_dif =
        std::clamp(dj.cmd_.pos_dif, dj.min_pos_dif_, dj.max_pos_dif_);
    dj.cmd_.pos_avg =
        std::clamp(dj.cmd_.pos_avg, dj.min_pos_avg_, dj.max_pos_avg_);

    dj.cmd_.loaded = true;
  }
};

}  // namespace gf3

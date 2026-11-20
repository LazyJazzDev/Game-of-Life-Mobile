#pragma once
#include <algorithm>

class SimulationClock {
 public:
  static constexpr int kLightning = 3;

  template <class Step>
  void Advance(double elapsed, bool playing, int speed, Step step) {
    if (speed != previous_speed_ || !playing) {
      // Entering lightning mode advances immediately, as before.
      accumulated_ = 0.0;
      lightning_batch_ = 1;
    }
    previous_speed_ = speed;
    if (!playing)
      return;
    if (speed == kLightning) {
      // Batch generations independently of display refresh. Increase work while
      // there is frame-time headroom, back off to preserve input responsiveness.
      // The bound limits one command submission, not generations per second.
      if (elapsed > 0 && elapsed < 0.025)
        lightning_batch_ = std::min(256, lightning_batch_ * 2);
      else if (elapsed > 0.04)
        lightning_batch_ = std::max(1, lightning_batch_ / 2);
      for (int i = 0; i < lightning_batch_; ++i)
        step();
      return;
    }
    const double multiplier = speed == 1 ? 2.0 : speed == 2 ? 5.0 : 1.0;
    accumulated_ += elapsed * multiplier;
    while (accumulated_ >= 0.5) {
      step();
      accumulated_ -= 0.5;
    }
  }

  double NextStepDelay(int speed) const {
    if (speed == kLightning)
      return 0.0;
    const double multiplier = speed == 1 ? 2.0 : speed == 2 ? 5.0 : 1.0;
    return std::max(0.0, (0.5 - accumulated_) / multiplier);
  }

 private:
  double accumulated_{0.0};
  int previous_speed_{0};
  int lightning_batch_{1};
};

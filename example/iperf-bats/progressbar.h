/**
 * @file progressbar.h
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief Copied from https://github.com/aminnj/cpptqdm
 * @version 1.0.0
 * @date 2025-09-11
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_PROGRESSBAR_H_
#define SRC_EXAMPLE_IPERF_BATS_PROGRESSBAR_H_

#include <math.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <ios>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "src/include/time.h"

class CompoundProgressBar;
class ProgressBar {
 private:
  friend class CompoundProgressBar;
  // time, iteration counters and fixed-size circular buffers for rate calculations
  uint64_t t_first = get_time_in_us<ClockType::MONOTONIC>();
  uint64_t t_old = get_time_in_us<ClockType::MONOTONIC>();
  int64_t n_old = 0;

  static constexpr size_t MAX_SMOOTH = 75;
  std::array<uint64_t, MAX_SMOOTH> deq_t_arr{};
  std::array<int64_t, MAX_SMOOTH> deq_n_arr{};
  size_t deq_head = 0;
  size_t deq_size = 0;
  uint64_t deq_dtsum = 0.0;
  int64_t deq_dnsum = 0;
  int64_t nupdates = 0;

  int64_t total_ = 0;
  // period is stored as exponent: period = 1 << period_power
  int period_power = 0;
  uint64_t smoothing = 50;  // desired smoothing window (bounded by MAX_SMOOTH)
  uint64_t last_div_res = 0;
  bool use_ema = true;
  float alpha_ema = 0.1f;

  std::vector<std::string> bars = {" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};

  bool in_screen = (system("test $STY") == 0);
  bool in_tmux = (system("test $TMUX") == 0);
  bool is_tty = isatty(1);
  bool use_colors = true;
  bool color_transition = true;
  int64_t width = 40;

  std::string right_pad = "▏";
  std::string label = "";

  void hsv_to_rgb(float h, float s, float v, int64_t& r, int64_t& g, int64_t& b) const {
    if (s < 1e-6f) {
      v *= 255.0f;
      r = static_cast<int64_t>(v);
      g = static_cast<int64_t>(v);
      b = static_cast<int64_t>(v);
      return;
    }
    int64_t i = static_cast<int64_t>(h * 6.0f);
    float f = (h * 6.0f) - static_cast<float>(i);
    int64_t p = static_cast<int64_t>(255.0f * (v * (1.0f - s)));
    int64_t q = static_cast<int64_t>(255.0f * (v * (1.0f - s * f)));
    int64_t t = static_cast<int64_t>(255.0f * (v * (1.0f - s * (1.0f - f))));
    v *= 255.0f;
    i %= 6;
    int64_t vi = static_cast<int64_t>(v);
    if (i == 0) {
      r = vi;
      g = t;
      b = p;
    } else if (i == 1) {
      r = q;
      g = vi;
      b = p;
    } else if (i == 2) {
      r = p;
      g = vi;
      b = t;
    } else if (i == 3) {
      r = p;
      g = q;
      b = vi;
    } else if (i == 4) {
      r = t;
      g = p;
      b = vi;
    } else {  // i == 5
      r = vi;
      g = p;
      b = q;
    }
  }

  // Format a single bar line (no leading CR, no trailing CR/newline).
  // Use this from a multi-line drawer.
  std::string format_line(int64_t curr, int64_t tot) {
    // update internal timing/rate buffers but do NOT emit to stdout here.
    uint64_t cur_div =
        (period_power == 0) ? static_cast<uint64_t>(curr) : (static_cast<uint64_t>(curr) >> period_power);
    if (cur_div <= last_div_res && curr != tot) {
      // internal guard: only update samples if progress is moving forward
      return "";
    }

    last_div_res = cur_div;
    ++nupdates;
    auto now = get_time_in_us<ClockType::MONOTONIC>();
    auto dt = now - t_old;
    auto dt_tot = now - t_first;

    int64_t dn = curr - n_old;
    n_old = curr;
    t_old = now;
    // push sample into circular buffers
    size_t cap = static_cast<size_t>(std::min<uint64_t>(smoothing, MAX_SMOOTH));
    if (cap == 0) cap = 1;
    if (deq_size == cap) {
      deq_dtsum -= deq_t_arr[deq_head];
      deq_dnsum -= deq_n_arr[deq_head];
      deq_t_arr[deq_head] = dt;
      deq_n_arr[deq_head] = dn;
      deq_dtsum += dt;
      deq_dnsum += dn;
      deq_head = (deq_head + 1) % cap;
    } else {
      deq_t_arr[(deq_head + deq_size) % cap] = dt;
      deq_n_arr[(deq_head + deq_size) % cap] = dn;
      deq_dtsum += dt;
      deq_dnsum += dn;
      ++deq_size;
    }

    double avgrate = 0.0;  // B/s or Hz
    if (use_ema && deq_size > 0) {
      // EMA over recent samples, start from newest for responsiveness
      size_t idx = (deq_head + deq_size - 1) % cap;
      avgrate = (static_cast<double>(deq_n_arr[idx]) / deq_t_arr[idx]) * 1000000;
      for (size_t i = 1; i < deq_size; ++i) {
        idx = (deq_head + deq_size - 1 - i + cap) % cap;
        double r = (static_cast<double>(deq_n_arr[idx]) / deq_t_arr[idx]) * 1000000;
        avgrate = alpha_ema * r + (1.0 - alpha_ema) * avgrate;
      }
    } else if (deq_dtsum > 0.0) {
      avgrate = (static_cast<double>(deq_dnsum) / deq_dtsum) * 1000000;
    }

    // Recompute period_power occasionally
    if (nupdates > 10) {
      int64_t raw_period = static_cast<int64_t>(std::min(std::max((1.0 / 25.0) * curr / dt_tot, 1.0), 500000.0));
      smoothing = std::min<uint64_t>(MAX_SMOOTH, 25 * 3);
      int p = 0;
      int64_t pow = 1;
      while (pow < raw_period) {
        pow <<= 1;
        ++p;
      }
      period_power = p;
      last_div_res = (period_power == 0) ? static_cast<uint64_t>(curr) : (static_cast<uint64_t>(curr) >> period_power);
    }
    // store last computed average in total_ for rendering fallback
    total_ = tot;

    // Now build the textual line (same formatting used before)
    // compute stats with current deq sums (do not mutate timing state here)
    int64_t cur_period = (period_power == 0) ? 1 : (1LL << period_power);
    double peta = (avgrate > 0.0) ? (tot - curr) / avgrate : 0.0;
    double pct = tot ? (static_cast<double>(curr) / static_cast<double>(tot) * 100.0) : 0.0;
    if ((tot - curr) <= cur_period) {
      pct = 100.0;
      if (t_old > t_first) {
        avgrate = (static_cast<double>(tot) / (t_old - t_first)) * 1000000;
      }
      curr = tot;
      peta = 0.0;
    }

    // build output into one string (no leading carriage return)
    std::string out;
    out.reserve(static_cast<size_t>(width) + 200);

    if (use_colors) {
      if (color_transition) {
        int64_t r = 255, g = 255, b = 255;
        hsv_to_rgb(0.0f + 0.01f * static_cast<float>(pct) / 3.0f, 0.65f, 1.0f, r, g, b);
        char tmp[64];
        int n = std::snprintf(tmp, sizeof(tmp), "\033[38;2;%ld;%ld;%ldm ", r, g, b);
        out.append(tmp, static_cast<size_t>(n));
      } else {
        out.append("\033[32m ");
      }
    }

    double fills_d = (tot ? (static_cast<double>(curr) / static_cast<double>(tot) * width) : 0.0);
    int64_t fills = static_cast<int64_t>(fills_d);
    if (fills < 0) fills = 0;
    if (fills > width) fills = width;

    // append full blocks
    out.append(std::string(static_cast<size_t>(fills), bars.back()[0]));
    // fractional glyph if not in_screen and not finished
    if (!in_screen && curr != tot) {
      int frac = static_cast<int>(8.0 * (fills_d - fills));
      if (frac < 0) frac = 0;
      if (frac > 8) frac = 8;
      out.append(bars[frac]);
    }
    // padding
    int64_t pad = width - fills - 1;
    if (pad > 0) out.append(std::string(static_cast<size_t>(pad), bars.front()[0]));

    out.append(" ");
    out.append(right_pad);

    if (use_colors) out.append("\033[1m\033[31m");
    {
      char tmp[64];
      int n = std::snprintf(tmp, sizeof(tmp), " %4.1f%% ", pct);
      out.append(tmp, static_cast<size_t>(n));
    }
    if (use_colors) out.append("\033[34m");

    // speed unit
    const char* unit = "B/s";
    double div = 1.0;
    if (avgrate > 1e6) {
      unit = "MB/s";
      div = 1.0e6;
    } else if (avgrate > 1e3) {
      unit = "KB/s";
      div = 1.0e3;
    }
    {
      int64_t passed_s = (now - t_first) / 1000000.0;
      int64_t passed_ms = (now - t_first) / 1000.0 - passed_s * 1000;
      char tmp[128];
      int n = std::snprintf(tmp, sizeof(tmp), "[%4ld/%4ld Bytes | %3.1f %s | time %lds:%ldms] ", curr, tot,
                            avgrate / div, unit, passed_s, passed_ms);
      out.append(tmp, static_cast<size_t>(n));
    }

    out.append(label);
    if (use_colors) out.append("\033[0m");
    return out;
  }

 public:
  ProgressBar() {
    if (in_screen) {
      set_theme_basic();
      color_transition = false;
    } else if (in_tmux) {
      color_transition = false;
    }
  }

  void reset() {
    t_first = get_time_in_us<ClockType::MONOTONIC>();
    t_old = get_time_in_us<ClockType::MONOTONIC>();
    n_old = 0;
    deq_head = 0;
    deq_size = 0;
    deq_dtsum = 0.0;
    deq_dnsum = 0;
    period_power = 0;
    nupdates = 0;
    total_ = 0;
    label.clear();
  }

  void set_theme_line() { bars = {"─", "─", "─", "╾", "╾", "╾", "╾", "━", "═"}; }
  void set_theme_circle() { bars = {" ", "◓", "◑", "◒", "◐", "◓", "◑", "◒", "#"}; }
  void set_theme_braille() { bars = {" ", "⡀", "⡄", "⡆", "⡇", "⡏", "⡟", "⡿", "⣿"}; }
  void set_theme_braille_spin() { bars = {" ", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠇", "⠿"}; }
  void set_theme_vertical() { bars = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█", "█"}; }
  void set_theme_basic() {
    bars = {" ", " ", " ", " ", " ", " ", " ", " ", "#"};
    right_pad = "|";
  }
  void set_label(const std::string& label_) {
    label = label_;
    // call when registering bar
    t_first = get_time_in_us<ClockType::MONOTONIC>();
    t_old = get_time_in_us<ClockType::MONOTONIC>();
  }

  void finish() { progress(total_, total_); }

  // backward-compatible single-line emitter (keeps previous behaviour)
  void progress(int64_t curr, int64_t tot) {
    std::string line = format_line(curr, tot);
    if (line.empty()) {
      // not time to draw yet
      return;
    }
    // single-line output: carriage return + line (no newline) so it overwrites the same line
    std::string out = "\r " + line + "\r ";
    fwrite(out.data(), 1, out.size(), stdout);
    int64_t cur_period = (period_power == 0) ? 1 : (1LL << period_power);
    if ((tot - curr) > cur_period || curr == tot) std::fflush(stdout);
  }
};

class CompoundProgressBar {
 private:
  static constexpr uint16_t max_bars = 10;
  std::array<ProgressBar, max_bars> bars{};
  std::array<std::pair<int64_t, int64_t>, max_bars> states{};  // curr, tot per bar
  bool initialized = false;
  bool is_tty = isatty(1);
  bool layout_initialized = false;

  // Keep terminal operations serialized
  std::mutex term_mutex;
  // Print initial blank block so later we can in-place update lines
  void ensure_layout_locked() {
    if (layout_initialized) return;
    if (!is_tty) {
      layout_initialized = true;
      return;
    }
    std::string init;
    init.reserve(max_bars * 8);
    for (uint16_t i = 0; i < max_bars; ++i) {
      init.append("\r\033[2K\n");  // clear line then newline
    }
    fwrite(init.data(), 1, init.size(), stdout);
    fflush(stdout);
    layout_initialized = true;
  }

  // Redraw exactly one line (idx) in-place
  void draw_line(uint16_t idx) {
    const auto& st = states[idx];
    if (st.second == 0) return;  // nothing meaningful yet
    std::string line = bars[idx].format_line(st.first, st.second);
    if (line.empty()) return;  // not time to draw yet

    if (!is_tty) {
      // Non-TTY: just print appended lines (no in-place update)
      const auto& st = states[idx];
      if (st.second != 0 && !line.empty()) {
        line.append("\n");
        fwrite(line.data(), 1, line.size(), stdout);
        fflush(stdout);
      }
      return;
    }

    std::scoped_lock lock(term_mutex);
    ensure_layout_locked();

    // We assume cursor currently sits just after the block (bottom line after last newline).
    // Move up (max_bars - idx) lines to reach start of target line.
    // Print cleared updated line + newline.
    // Move back down (max_bars - idx - 1) lines to restore cursor at bottom.
    uint16_t lines_up = static_cast<uint16_t>(max_bars - idx);
    uint16_t lines_down = static_cast<uint16_t>(max_bars - idx - 1);

    char seq[64];
    std::string out;
    out.reserve(line.size() + 64);

    // Move up
    std::snprintf(seq, sizeof(seq), "\033[%uA", lines_up);
    out.append(seq);

    // Clear and write line
    out.append("\r\033[2K");
    out.append(line);
    out.push_back('\n');

    // Move down
    if (lines_down > 0) {
      std::snprintf(seq, sizeof(seq), "\033[%uB", lines_down);
      out.append(seq);
    }
    // Ensure cursor at start of bottom (carriage return)
    out.append("\r");

    fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout);
  }

  // Update state only (no drawing)
  bool update_state(uint16_t idx, int64_t curr, int64_t tot) {
    if (idx >= max_bars) return false;
    std::scoped_lock lock(term_mutex);
    states[idx] = {curr, tot};
    return true;
  }

 public:
  // Register bar index & optional label
  bool register_bar(uint16_t idx, const std::string& label = "") {
    if (idx >= max_bars) return false;
    std::scoped_lock lock(term_mutex);
    if (!label.empty()) bars[idx].set_label(label);
    // Layout will be (re)created on first draw after all registrations
    return true;
  }

  // Update state and redraw only that bar's terminal line
  void update_bar(uint16_t idx, int64_t curr, int64_t tot) {
    if (!update_state(idx, curr, tot)) return;
    draw_line(idx);
  }

  // Finish: print final states and add extra newline
  void finish(uint16_t idx) {
    if (!is_tty) return;
    if (!layout_initialized) return;
    const auto& st = states[idx];
    update_bar(idx, st.second, st.second);
  }

  void reset(uint16_t idx) {
    std::scoped_lock lock(term_mutex);
    auto& st = states[idx];
    st.first = 0;
    st.second = 0;
    bars[idx].reset();
  }
};

#endif  // SRC_EXAMPLE_IPERF_BATS_PROGRESSBAR_H_

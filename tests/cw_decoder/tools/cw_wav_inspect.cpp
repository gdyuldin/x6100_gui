// Offline inspector for the cw_decoder CW decoder.
//
// Not a test: it is not registered with add_test (see tests/cw_decoder/CMakeLists.txt)
// so ctest stays fast. Build and run it manually:
//   cmake --build build_test --target cw_wav_inspect
//   ./build_test/tests/cw_decoder/cw_wav_inspect <file.wav> <threshold_db> [options]
//
// It reads a 4000 Hz / 16-bit PCM WAV recording and feeds it through the real
// cw::CwReceiver (the same audio->FFT->tone tracker->detector->classifier->
// decoder path as on the device), then prints the decoded text and writes a
// self-contained SVG with four time-aligned panels: audio amplitude, the NCO
// frequency of the coherent tone tracker, the frame SNR with the user
// threshold plus the detector's adaptive t_on/t_off levels, and the committed
// ON/OFF state of the detector. The SVG width grows with the recording length
// so the detector step stays readable on long files.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "cw_config.h"
#include "cw_receiver.h"
#include "cw_receiver_diag_access.h"
#include "time_classifier.h"
#include "wav_reader.h"

using cw::CwReceiver;
using Diag = cw::CwReceiverDiagAccess;

namespace {

/***** LAYOUT *****/

constexpr int SVG_MIN_WIDTH = 1400;
constexpr int SVG_LEFT      = 62;
constexpr int SVG_RIGHT     = 20;
constexpr int SVG_TOP       = 22;
constexpr int SVG_PANEL_H   = 150;
constexpr int SVG_PANEL_GAP = 36;
constexpr int SVG_BOTTOM    = 40;
constexpr int SVG_PAD       = 8;

constexpr int PANEL_COUNT = 4;

// Horizontal density of the time axis: ~1.6 px per 8 ms frame at the default.
constexpr double PX_PER_SEC_DEFAULT = 200.0;

// on_frame_ fires once per coherent integrator hop, not once per FFT frame.
constexpr double FRAME_MS = static_cast<double>(cw::COHERENT_INTEGRATOR_HOP) * 1000.0 / cw::SAMPLE_RATE;

// SNR panel window, in dB relative to the user threshold.
constexpr double SNR_MIN_DB = -40.0;
constexpr double SNR_MAX_DB = 40.0;

// NCO panel: minimum Y span so a near-constant trace is not a flat line.
constexpr double NCO_MIN_SPAN_HZ = 20.0;

// Default NCO start frequency (CoherentToneTracker ctor set_freq).
constexpr double NCO_DEFAULT_HZ = 700.0;

constexpr float MAX_THRESHOLD_DB = 60.0f;

/***** STRUCTS *****/

struct Options {
    std::string file;
    float       threshold  = 10.0f;
    float       hpf        = CwReceiver::DEFAULT_HPF_HZ;
    float       lpf        = CwReceiver::DEFAULT_LPF_HZ;
    double      shift_ms   = 0.0;
    std::string out;
    std::string text_file;
    int         audio_cols = 0; // <= 0 => follow the plot width (1 column/px)
    double      px_per_sec = PX_PER_SEC_DEFAULT;
    int         width_max  = 0; // 0 => no cap
};

// One frame sampled inside CwReceiver::on_frame, which runs once per coherent
// integrator hop (8 ms) with the committed state already updated.
struct FrameSample {
    double t_ms          = 0.0;
    float  snr_db        = 0.0f;
    float  nco_hz        = 0.0f;
    float  peak_freq_hz  = 0.0f;
    float  snr_lin       = 0.0f;
    float  t_on_db       = 0.0f;
    float  t_off_db      = 0.0f;
    int    stable_frames = 0;
    bool   retuned       = false;
    bool   detector_on   = false;
};

/***** SMALL HELPERS *****/

std::string num(double value, int precision = 1) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    return std::string(buf);
}

// Detector thresholds are linear power/noise ratios; the SNR panel plots dB.
float lin_to_db(float lin) {
    return 10.0f * std::log10(lin + 1e-12f);
}

std::string escape_xml(const std::string &text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

// Preferred grid step (ms) that keeps the X axis around ten divisions.
double choose_time_step(double range_ms) {
    static const double CANDIDATES[] = {10,  20,  50,   100,   200,   500,    1000,
                                        2000, 5000, 10000, 20000, 30000, 60000, 120000, 300000};
    const double        target       = range_ms / 10.0;
    for (double candidate : CANDIDATES) {
        if (candidate >= target)
            return candidate;
    }
    return CANDIDATES[sizeof(CANDIDATES) / sizeof(CANDIDATES[0]) - 1];
}

/***** ARGUMENTS *****/

void print_usage(const char *argv0) {
    std::fprintf(stderr,
                 "usage: %s <file.wav> <threshold_db> [options]\n"
                 "  4000 Hz / 16-bit PCM WAV only (stereo -> left channel).\n"
                 "Options:\n"
                 "  --hpf HZ         high-pass / search lower edge (default %.0f)\n"
                 "  --lpf HZ         low-pass  / search upper edge (default %.0f)\n"
                 "  --shift-ms MS    shift the detected series on the plot (default 0)\n"
                 "  --out FILE.svg   output SVG (default <file>.svg)\n"
                 "  --text FILE      also write the decoded text to FILE\n"
                 "  --audio-cols N   audio decimation columns (default: follow width)\n"
                 "  --px-per-sec N   time-axis density (default %.0f)\n"
                 "  --width-max N    cap the SVG width (default 0 = no cap)\n",
                 argv0, static_cast<double>(CwReceiver::DEFAULT_HPF_HZ),
                 static_cast<double>(CwReceiver::DEFAULT_LPF_HZ), PX_PER_SEC_DEFAULT);
}

// Consumes the value of an option or reports a missing-argument error.
bool option_value(int argc, char **argv, int &i, const char *name, std::string &value) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s needs an argument\n", name);
        return false;
    }
    value = argv[++i];
    return true;
}

bool parse_args(int argc, char **argv, Options &opts) {
    if (argc < 3) {
        print_usage(argv[0]);
        return false;
    }
    opts.file      = argv[1];
    opts.threshold = static_cast<float>(std::atof(argv[2]));

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string       value;
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return false;
        } else if (arg == "--hpf") {
            if (!option_value(argc, argv, i, "--hpf", value)) return false;
            opts.hpf = static_cast<float>(std::atof(value.c_str()));
        } else if (arg == "--lpf") {
            if (!option_value(argc, argv, i, "--lpf", value)) return false;
            opts.lpf = static_cast<float>(std::atof(value.c_str()));
        } else if (arg == "--shift-ms") {
            if (!option_value(argc, argv, i, "--shift-ms", value)) return false;
            opts.shift_ms = std::atof(value.c_str());
        } else if (arg == "--out") {
            if (!option_value(argc, argv, i, "--out", value)) return false;
            opts.out = value;
        } else if (arg == "--text") {
            if (!option_value(argc, argv, i, "--text", value)) return false;
            opts.text_file = value;
        } else if (arg == "--audio-cols") {
            if (!option_value(argc, argv, i, "--audio-cols", value)) return false;
            opts.audio_cols = std::atoi(value.c_str());
        } else if (arg == "--px-per-sec") {
            if (!option_value(argc, argv, i, "--px-per-sec", value)) return false;
            opts.px_per_sec = std::atof(value.c_str());
        } else if (arg == "--width-max") {
            if (!option_value(argc, argv, i, "--width-max", value)) return false;
            opts.width_max = std::atoi(value.c_str());
        } else {
            std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
            return false;
        }
    }

    if (opts.out.empty())
        opts.out = opts.file + ".svg";
    if (opts.px_per_sec <= 0.0) {
        std::fprintf(stderr, "error: --px-per-sec must be positive\n");
        return false;
    }
    if (opts.width_max < 0) {
        std::fprintf(stderr, "error: --width-max must be >= 0\n");
        return false;
    }
    if (!(opts.threshold >= -60.0f && opts.threshold <= MAX_THRESHOLD_DB)) {
        std::fprintf(stderr, "error: threshold out of range ([-60, %.0f])\n", static_cast<double>(MAX_THRESHOLD_DB));
        return false;
    }
    if (opts.hpf < 0.0f || opts.lpf <= opts.hpf) {
        std::fprintf(stderr, "error: need 0 <= hpf < lpf\n");
        return false;
    }
    return true;
}

/***** RUN *****/

void run_receiver(const Options &opts, const std::vector<float> &samples, std::string &text,
                  std::vector<FrameSample> &frames) {
    CwReceiver *self = nullptr;
    CwReceiver  rx([&text](const char *chunk) { text += chunk; }, [](bool) {}, [&]() {
        const CwReceiver &r = *self;
        FrameSample       fs;
        fs.t_ms          = (static_cast<double>(frames.size()) + 1.0) * FRAME_MS;
        fs.snr_db        = Diag::snr_db(r);
        fs.nco_hz        = Diag::nco_hz(r);
        fs.peak_freq_hz  = Diag::peak_freq_hz(r);
        fs.snr_lin       = Diag::snr_lin(r);
        fs.t_on_db       = lin_to_db(Diag::t_on_lin(r));
        fs.t_off_db      = lin_to_db(Diag::t_off_lin(r));
        fs.stable_frames = Diag::stable_frames(r);
        fs.retuned       = Diag::retuned(r);
        fs.detector_on   = Diag::is_active(r);
        frames.push_back(fs);
    });
    self = &rx;

    rx.change_threshold(opts.threshold);
    rx.change_hpf_hz(opts.hpf);
    rx.change_lpf_hz(opts.lpf);

    rx.process_audio_frame(samples.size(), const_cast<float *>(samples.data()));
}

/***** SVG *****/

class SvgPlot {
  public:
    SvgPlot(const Options &opts, const std::vector<float> &samples, const std::vector<FrameSample> &frames)
        : opts_(opts), samples_(samples), frames_(frames) {
        const double duration_s = static_cast<double>(samples_.size()) / static_cast<double>(cw::SAMPLE_RATE);
        const int    axis_w     = static_cast<int>(std::lround(duration_s * opts_.px_per_sec));
        width_                  = std::max(SVG_MIN_WIDTH, SVG_LEFT + SVG_RIGHT + axis_w);
        if (opts_.width_max > 0)
            width_ = std::min(width_, opts_.width_max);
        plot_w_ = width_ - SVG_LEFT - SVG_RIGHT;
        height_ = SVG_TOP + PANEL_COUNT * SVG_PANEL_H + (PANEL_COUNT - 1) * SVG_PANEL_GAP + SVG_BOTTOM;

        const double audio_ms = static_cast<double>(samples_.size()) * 1000.0 / static_cast<double>(cw::SAMPLE_RATE);
        const double frame_ms = frames_.empty() ? 0.0 : frames_.back().t_ms + FRAME_MS + opts_.shift_ms;
        x_min_                = std::min(0.0, opts_.shift_ms);
        x_max_                = std::max(audio_ms, frame_ms);
        if (x_max_ <= x_min_)
            x_max_ = x_min_ + 1.0;
    }

    std::string render() const;

  private:
    int px(double t_ms) const {
        const double f = (t_ms - x_min_) / (x_max_ - x_min_);
        return static_cast<int>(std::lround(SVG_LEFT + f * plot_w_));
    }

    int panel_top(int index) const { return SVG_TOP + index * (SVG_PANEL_H + SVG_PANEL_GAP); }

    static int py(double value, double vmin, double vmax, int top) {
        double f = (value - vmin) / (vmax - vmin);
        f        = std::clamp(f, 0.0, 1.0);
        const int inner = SVG_PANEL_H - 2 * SVG_PAD;
        return top + SVG_PAD + static_cast<int>(std::lround((1.0 - f) * inner));
    }

    void draw_grid(std::ostringstream &svg, bool with_labels) const;
    void draw_audio(std::ostringstream &svg) const;
    void draw_nco(std::ostringstream &svg) const;
    void draw_snr(std::ostringstream &svg) const;
    void draw_detector(std::ostringstream &svg) const;

    const Options                  &opts_;
    const std::vector<float>       &samples_;
    const std::vector<FrameSample> &frames_;
    int    width_  = SVG_MIN_WIDTH;
    int    plot_w_ = 0;
    int    height_ = 0;
    double x_min_  = 0.0;
    double x_max_  = 1.0;
};

void SvgPlot::draw_grid(std::ostringstream &svg, bool with_labels) const {
    const double step = choose_time_step(x_max_ - x_min_);
    for (double t = 0.0; t <= x_max_; t += step) {
        if (t < x_min_)
            continue;
        const int x = px(t);
        for (int panel = 0; panel < PANEL_COUNT; ++panel) {
            const int top = panel_top(panel);
            svg << "<line x1=\"" << x << "\" y1=\"" << top << "\" x2=\"" << x << "\" y2=\"" << (top + SVG_PANEL_H)
                << "\" stroke=\"#eeeeee\" stroke-width=\"1\"/>";
        }
        if (with_labels) {
            const int top = panel_top(PANEL_COUNT - 1) + SVG_PANEL_H;
            svg << "<text x=\"" << x << "\" y=\"" << (top + 14) << "\" text-anchor=\"middle\" font-family=\"monospace\" "
                << "font-size=\"10\" fill=\"#555\">" << num(t / 1000.0, t < 10000.0 ? 2 : 0) << "s</text>";
        }
    }
}

void SvgPlot::draw_audio(std::ostringstream &svg) const {
    const int    top  = panel_top(0);
    const size_t n    = samples_.size();
    svg << "<rect x=\"" << SVG_LEFT << "\" y=\"" << top << "\" width=\"" << plot_w_ << "\" height=\"" << SVG_PANEL_H
        << "\" fill=\"none\" stroke=\"#cccccc\"/>";
    svg << "<text x=\"" << SVG_LEFT << "\" y=\"" << (top - 5)
        << "\" font-family=\"monospace\" font-size=\"11\" fill=\"#222\">Audio (+/-1)</text>";

    const int y0 = py(0.0, -1.0, 1.0, top);
    svg << "<line x1=\"" << SVG_LEFT << "\" y1=\"" << y0 << "\" x2=\"" << (SVG_LEFT + plot_w_) << "\" y2=\"" << y0
        << "\" stroke=\"#dddddd\" stroke-width=\"1\"/>";

    if (n == 0)
        return;

    const int    want = opts_.audio_cols > 0 ? opts_.audio_cols : plot_w_;
    const int    cols = std::min<int>(want, static_cast<int>(n));
    const double per  = static_cast<double>(n) / cols;
    for (int c = 0; c < cols; ++c) {
        const size_t s0 = static_cast<size_t>(c * per);
        size_t       s1 = static_cast<size_t>((c + 1) * per);
        if (s1 <= s0)
            s1 = s0 + 1;
        if (s1 > n)
            s1 = n;

        float lo = samples_[s0];
        float hi = samples_[s0];
        for (size_t s = s0; s < s1; ++s) {
            lo = std::min(lo, samples_[s]);
            hi = std::max(hi, samples_[s]);
        }
        const int x = SVG_LEFT + static_cast<int>(std::lround((static_cast<double>(c) + 0.5) * plot_w_ / cols));
        const int yt = py(hi, -1.0, 1.0, top);
        const int yb = py(lo, -1.0, 1.0, top);
        svg << "<line x1=\"" << x << "\" y1=\"" << yt << "\" x2=\"" << x << "\" y2=\"" << yb
            << "\" stroke=\"#2a62c0\" stroke-width=\"1\"/>";
    }
}

void SvgPlot::draw_nco(std::ostringstream &svg) const {
    const int top = panel_top(1);

    svg << "<rect x=\"" << SVG_LEFT << "\" y=\"" << top << "\" width=\"" << plot_w_ << "\" height=\"" << SVG_PANEL_H
        << "\" fill=\"none\" stroke=\"#cccccc\"/>";
    svg << "<text x=\"" << SVG_LEFT << "\" y=\"" << (top - 5)
        << "\" font-family=\"monospace\" font-size=\"11\" fill=\"#222\">NCO frequency, Hz</text>";

    // Y-range from the data, padded, with a minimum span so a near-constant
    // trace is not a flat line glued to an edge.
    double vmin = std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();
    for (const FrameSample &fs : frames_) {
        if (!std::isfinite(fs.nco_hz))
            continue;
        vmin = std::min(vmin, static_cast<double>(fs.nco_hz));
        vmax = std::max(vmax, static_cast<double>(fs.nco_hz));
    }
    if (!std::isfinite(vmin) || !std::isfinite(vmax)) {
        vmin = NCO_DEFAULT_HZ - NCO_MIN_SPAN_HZ * 0.5;
        vmax = NCO_DEFAULT_HZ + NCO_MIN_SPAN_HZ * 0.5;
    }
    const double span = vmax - vmin;
    if (span < NCO_MIN_SPAN_HZ) {
        const double mid = 0.5 * (vmin + vmax);
        vmin             = mid - NCO_MIN_SPAN_HZ * 0.5;
        vmax             = mid + NCO_MIN_SPAN_HZ * 0.5;
    } else {
        const double pad = 0.1 * span;
        vmin -= pad;
        vmax += pad;
    }

    // Y labels.
    svg << "<text x=\"" << (SVG_LEFT - 6) << "\" y=\"" << (py(vmax, vmin, vmax, top) + 4)
        << "\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"9\" fill=\"#777\">" << num(vmax, 0)
        << "</text>";
    svg << "<text x=\"" << (SVG_LEFT - 6) << "\" y=\"" << (py(vmin, vmin, vmax, top) + 4)
        << "\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"9\" fill=\"#777\">" << num(vmin, 0)
        << "</text>";

    // Dashed reference at the NCO's default start frequency.
    if (NCO_DEFAULT_HZ >= vmin && NCO_DEFAULT_HZ <= vmax) {
        const int y_ref = py(NCO_DEFAULT_HZ, vmin, vmax, top);
        svg << "<line x1=\"" << SVG_LEFT << "\" y1=\"" << y_ref << "\" x2=\"" << (SVG_LEFT + plot_w_) << "\" y2=\""
            << y_ref << "\" stroke=\"#888888\" stroke-width=\"1\" stroke-dasharray=\"6 4\"/>";
    }

    if (frames_.empty())
        return;

    // Break the polyline on non-finite samples.
    std::ostringstream path;
    bool               open = false;
    for (const FrameSample &fs : frames_) {
        if (!std::isfinite(fs.nco_hz)) {
            open = false;
            continue;
        }
        const int x = px(fs.t_ms + opts_.shift_ms);
        const int y = py(fs.nco_hz, vmin, vmax, top);
        path << (open ? " L" : " M") << x << " " << y;
        open = true;
    }
    svg << "<path d=\"" << path.str() << "\" fill=\"none\" stroke=\"#2a62c0\" stroke-width=\"1.2\"/>";

    // Vertical markers where the tracker retuned the NCO.
    for (const FrameSample &fs : frames_) {
        if (!fs.retuned)
            continue;
        const int x = px(fs.t_ms + opts_.shift_ms);
        svg << "<line x1=\"" << x << "\" y1=\"" << top << "\" x2=\"" << x << "\" y2=\"" << (top + SVG_PANEL_H)
            << "\" stroke=\"#c02020\" stroke-width=\"1\" stroke-dasharray=\"3 3\"/>";
    }
}

void SvgPlot::draw_snr(std::ostringstream &svg) const {
    const int    top  = panel_top(2);
    const double thr  = static_cast<double>(opts_.threshold);
    const double vmin = thr + SNR_MIN_DB;
    const double vmax = thr + SNR_MAX_DB;

    svg << "<rect x=\"" << SVG_LEFT << "\" y=\"" << top << "\" width=\"" << plot_w_ << "\" height=\"" << SVG_PANEL_H
        << "\" fill=\"none\" stroke=\"#cccccc\"/>";
    svg << "<text x=\"" << SVG_LEFT << "\" y=\"" << (top - 5)
        << "\" font-family=\"monospace\" font-size=\"11\" fill=\"#222\">SNR, dB (10 log10(power/noise)), detector "
           "thresholds</text>";

    // Y labels.
    svg << "<text x=\"" << (SVG_LEFT - 6) << "\" y=\"" << (py(vmax, vmin, vmax, top) + 4)
        << "\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"9\" fill=\"#777\">" << num(vmax, 0)
        << "</text>";
    svg << "<text x=\"" << (SVG_LEFT - 6) << "\" y=\"" << (py(vmin, vmin, vmax, top) + 4)
        << "\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"9\" fill=\"#777\">" << num(vmin, 0)
        << "</text>";

    // Reference line: the user floor (the detector's absolute opening level).
    const int y_thr = py(thr, vmin, vmax, top);
    svg << "<line x1=\"" << SVG_LEFT << "\" y1=\"" << y_thr << "\" x2=\"" << (SVG_LEFT + plot_w_) << "\" y2=\"" << y_thr
        << "\" stroke=\"#888888\" stroke-width=\"1\" stroke-dasharray=\"6 4\"/>";

    if (frames_.empty())
        return;

    // Break the polyline on non-finite samples.
    std::ostringstream path;
    bool               open = false;
    for (const FrameSample &fs : frames_) {
        if (!std::isfinite(fs.snr_db)) {
            open = false;
            continue;
        }
        const int x = px(fs.t_ms + opts_.shift_ms);
        const int y = py(fs.snr_db, vmin, vmax, top);
        path << (open ? " L" : " M") << x << " " << y;
        open = true;
    }
    svg << "<path d=\"" << path.str() << "\" fill=\"none\" stroke=\"#c05000\" stroke-width=\"1.2\"/>";

    // Adaptive Schmitt thresholds of the detector, on the same dB scale: t_on
    // opens the detector, t_off releases it.
    auto series_path = [&](float FrameSample::*field) {
        std::ostringstream d;
        bool               open_path = false;
        for (const FrameSample &fs : frames_) {
            const float value = fs.*field;
            if (!std::isfinite(value)) {
                open_path = false;
                continue;
            }
            d << (open_path ? " L" : " M") << px(fs.t_ms + opts_.shift_ms) << " " << py(value, vmin, vmax, top);
            open_path = true;
        }
        return d.str();
    };
    svg << "<path d=\"" << series_path(&FrameSample::t_on_db) << "\" fill=\"none\" stroke=\"#c02020\" stroke-width=\"1"
        << "\" stroke-dasharray=\"4 3\"/>";
    svg << "<path d=\"" << series_path(&FrameSample::t_off_db) << "\" fill=\"none\" stroke=\"#208040\" stroke-width=\"1"
        << "\" stroke-dasharray=\"4 3\"/>";
    svg << "<text x=\"" << (SVG_LEFT + 8) << "\" y=\"" << (top + 14)
        << "\" font-family=\"monospace\" font-size=\"10\" fill=\"#c02020\">t_on</text>";
    svg << "<text x=\"" << (SVG_LEFT + 48) << "\" y=\"" << (top + 14)
        << "\" font-family=\"monospace\" font-size=\"10\" fill=\"#208040\">t_off</text>";
}

void SvgPlot::draw_detector(std::ostringstream &svg) const {
    const int top = panel_top(3);
    svg << "<rect x=\"" << SVG_LEFT << "\" y=\"" << top << "\" width=\"" << plot_w_ << "\" height=\"" << SVG_PANEL_H
        << "\" fill=\"none\" stroke=\"#cccccc\"/>";
    svg << "<text x=\"" << SVG_LEFT << "\" y=\"" << (top - 5)
        << "\" font-family=\"monospace\" font-size=\"11\" fill=\"#222\">Detector state (ON/OFF)</text>";

    const int y_on  = top + 36;
    const int y_off = top + 112;
    svg << "<text x=\"" << (SVG_LEFT - 6) << "\" y=\"" << (y_on + 4)
        << "\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"9\" fill=\"#777\">ON</text>";
    svg << "<text x=\"" << (SVG_LEFT - 6) << "\" y=\"" << (y_off + 4)
        << "\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"9\" fill=\"#777\">OFF</text>";

    if (frames_.empty())
        return;

    std::ostringstream path;
    const double       x_last = frames_.back().t_ms + FRAME_MS;
    path << "M" << px(frames_[0].t_ms + opts_.shift_ms) << " " << (frames_[0].detector_on ? y_on : y_off);
    for (size_t i = 0; i < frames_.size(); ++i) {
        const double x1 = (i + 1 < frames_.size()) ? px(frames_[i + 1].t_ms + opts_.shift_ms)
                                                   : px(x_last + opts_.shift_ms);
        const int    y0 = frames_[i].detector_on ? y_on : y_off;
        path << " L" << x1 << " " << y0;
        if (i + 1 < frames_.size()) {
            const int y1 = frames_[i + 1].detector_on ? y_on : y_off;
            if (y1 != y0)
                path << " L" << x1 << " " << y1;
        }
    }
    svg << "<path d=\"" << path.str() << "\" fill=\"none\" stroke=\"#111111\" stroke-width=\"1.6\"/>";
}

std::string SvgPlot::render() const {
    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width_ << "\" height=\"" << height_
        << "\" viewBox=\"0 0 " << width_ << " " << height_ << "\">";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>";
    svg << "<text x=\"" << SVG_LEFT << "\" y=\"14\" font-family=\"monospace\" font-size=\"12\" fill=\"#222\">"
        << escape_xml(opts_.file) << "  thr=" << num(opts_.threshold, 1) << "dB  hpf/lpf=" << num(opts_.hpf, 0) << "/"
        << num(opts_.lpf, 0) << "Hz  shift=" << num(opts_.shift_ms, 0) << "ms  frames=" << frames_.size() << "</text>";

    draw_grid(svg, true);
    draw_audio(svg);
    draw_nco(svg);
    draw_snr(svg);
    draw_detector(svg);

    if (frames_.empty()) {
        svg << "<text x=\"" << (SVG_LEFT + 20) << "\" y=\"" << (panel_top(1) + 40)
            << "\" font-family=\"monospace\" font-size=\"14\" fill=\"#c02020\">no frames (audio too short?)</text>";
    }

    svg << "</svg>\n";
    return svg.str();
}

} // namespace

int main(int argc, char **argv) {
    Options opts;
    if (!parse_args(argc, argv, opts))
        return 2;

    const cw_tools::WavData wav = cw_tools::read_wav_4k(opts.file);
    if (!wav.ok) {
        std::fprintf(stderr, "error: %s\n", wav.error.c_str());
        return 1;
    }
    if (wav.samples.empty()) {
        std::fprintf(stderr, "error: no audio samples in %s\n", opts.file.c_str());
        return 1;
    }

    std::string              text;
    std::vector<FrameSample> frames;
    run_receiver(opts, wav.samples, text, frames);

    const double duration_s = static_cast<double>(wav.samples.size()) / static_cast<double>(cw::SAMPLE_RATE);

    std::printf("file:       %s\n", opts.file.c_str());
    std::printf("duration:   %.2f s (%zu samples @ %.0f Hz)\n", duration_s, wav.samples.size(),
                static_cast<double>(cw::SAMPLE_RATE));
    std::printf("threshold:  %.1f dB   hpf/lpf: %.0f/%.0f Hz\n", opts.threshold, opts.hpf, opts.lpf);
    std::printf("frames:     %zu\n", frames.size());
    std::printf("decoded:    %s\n", text.c_str());

    if (!opts.text_file.empty()) {
        std::ofstream tf(opts.text_file);
        if (!tf) {
            std::fprintf(stderr, "error: cannot write text file %s\n", opts.text_file.c_str());
        } else {
            tf << text;
            std::printf("text:       written to %s\n", opts.text_file.c_str());
        }
    }

    const std::string svg = SvgPlot(opts, wav.samples, frames).render();
    std::ofstream     of(opts.out);
    if (!of) {
        std::fprintf(stderr, "error: cannot write SVG %s\n", opts.out.c_str());
        return 1;
    }
    of << svg;
    std::printf("svg:        %s\n", opts.out.c_str());

    return 0;
}
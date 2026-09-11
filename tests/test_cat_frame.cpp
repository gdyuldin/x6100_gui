// test_cat_frame.cpp
// Host tests for Frame::process() and Frame::dump() — CI-V command handler
// logic exercising every registered command through a local SettingsManager
// with an in-memory SQLite DB.

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "cat/cat.h"
#include "cfg/db.h"
#include "cfg/settings_manager.h"
#include "cfg/storage_policy.h"

// <!-- Stubs for external C functions that cat handler code calls.
//
// These replace the production implementations (radio.c, meter.c, tx_info.c)
// which are not linked into the test binary. They must have C linkage to match
// the extern "C" declarations used by cat_frame.cpp.
extern "C" {
#include "meter.h"
#include "radio.h"
#include "tx_info.h"
}

extern "C" bool tx_info_refresh(uint8_t *, float *alc, float *pwr, float *vswr) {
    *alc = 0.0f;
    *pwr = 2.0f;
    *vswr = 1.0f;
    return false;
}

extern "C" int16_t meter_get_raw_db() { return 0; }
extern "C" radio_state_t radio_get_state() { return RADIO_RX; }
extern "C" void radio_set_ptt(bool) {}
// -->

// ---- protocol constants (CI-V / X6100) --------------------------------------

#define FRAME_PRE 0xFE
#define FRAME_END 0xFD

#define CODE_OK 0xFB
#define CODE_NG 0xFA

#define LOCAL_ADDRESS 0xA4

#define C_RD_FREQ 0x03
#define C_RD_MODE 0x04
#define C_SET_FREQ 0x05
#define C_SET_MODE 0x06
#define C_SET_VFO 0x07
#define C_CTL_SPLT 0x0f
#define C_SET_TS 0x10
#define C_CTL_ATT 0x11
#define C_CTL_LVL 0x14
#define C_RD_SQSM 0x15
#define C_CTL_FUNC 0x16
#define C_RD_TRXID 0x19
#define C_CTL_MEM 0x1a
#define C_CTL_PTT 0x1c
#define C_SEND_SEL_FREQ 0x25
#define C_SEND_SEL_MODE 0x26
#define C_CTL_SCP 0x27

#define S_VFOA 0x00
#define S_VFOB 0x01
#define S_XCHNG 0xb0

#define M_USB 0x01

#define MEM_DM_FG 0x06

// ---- TestDbGuard ------------------------------------------------------------
namespace {

struct TestDbGuard {
    sqlite3 *db = nullptr;

    TestDbGuard() {
        REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
        char *err = nullptr;
        int   rc  = sqlite3_exec(db,
                                 "CREATE TABLE IF NOT EXISTS params ("
                                     "  name TEXT PRIMARY KEY,"
                                     "  val  INTEGER"
                                     ");"
                                     "CREATE TABLE IF NOT EXISTS band_params("
                                     "  bands_id INTEGER,"
                                     "  name     TEXT,"
                                     "  val      INTEGER,"
                                     "  UNIQUE (bands_id, name) ON CONFLICT REPLACE"
                                     ");"
                                     "CREATE TABLE IF NOT EXISTS mode_params("
                                     "  mode INTEGER,"
                                     "  name TEXT,"
                                     "  val  INTEGER,"
                                     "  UNIQUE (mode, name) ON CONFLICT REPLACE"
                                     ");"
                                     "CREATE TABLE IF NOT EXISTS bands("
                                     "  id         INTEGER PRIMARY KEY,"
                                     "  name       TEXT,"
                                     "  start_freq INTEGER,"
                                     "  stop_freq  INTEGER,"
                                     "  type       INTEGER"
                                     ");"
                                     "CREATE TABLE IF NOT EXISTS transverter("
                                     "  id   INTEGER,"
                                     "  name TEXT,"
                                     "  val  INTEGER,"
                                     "  UNIQUE(id, name) ON CONFLICT REPLACE"
                                     ");",
                                 nullptr, nullptr, &err);
        REQUIRE(rc == SQLITE_OK);
        if (err)
            sqlite3_free(err);
        ParamsTable::Init(db);
        BandParamsTable::Init(db);
        ModeParamsTable::Init(db);
        BandsTable::Init(db);
        TransverterTable::Init(db);
    }

    ~TestDbGuard() {
        TransverterTable::Shutdown();
        BandsTable::Shutdown();
        ParamsTable::Shutdown();
        BandParamsTable::Shutdown();
        ModeParamsTable::Shutdown();
        if (db) {
            sqlite3_close(db);
        }
    }
};

} // namespace

// ---- helpers ---------------------------------------------------------------
static constexpr int32_t kHz = 1000;

// Build a raw CI-V frame vector from a command byte and payload
static std::vector<char> ci_v_frame(uint8_t cmd, const std::vector<uint8_t> &payload = {}) {
    std::vector<char> raw(payload.size() + 6);
    raw[0] = static_cast<char>(FRAME_PRE);
    raw[1] = static_cast<char>(FRAME_PRE);
    raw[2] = static_cast<char>(0xE0); // dst
    raw[3] = static_cast<char>(LOCAL_ADDRESS); // src
    raw[4] = static_cast<char>(cmd);
    std::copy(payload.begin(), payload.end(), raw.begin() + 5);
    raw.back() = static_cast<char>(FRAME_END);
    return raw;
}

static Frame frame_from(uint8_t cmd, const std::vector<uint8_t> &payload = {}) {
    auto raw = ci_v_frame(cmd, payload);
    return Frame(raw.data(), raw.size());
}

static uint8_t u8(const std::vector<char> &buf, size_t i) {
    return static_cast<uint8_t>(buf[i]);
}

// Pre-populate band 5 with basic VFO settings.
static void set_band_5(TestDbGuard &db) {
    StoragePolicy &b = storage_policy_for(StorageType::BAND);
    REQUIRE(b.save_int(5, "vfo", X6100_VFO_A) == SUCCESS);
    REQUIRE(b.save_int(5, "vfoa_freq", 7'100 * kHz) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_freq", 7'150 * kHz) == SUCCESS);
    REQUIRE(b.save_int(5, "vfoa_mode", x6100_mode_usb) == SUCCESS);
    REQUIRE(b.save_int(5, "vfob_mode", x6100_mode_nfm) == SUCCESS);
}

static void init_band_5(TestDbGuard &db, SettingsManager &mgr) {
    set_band_5(db);
    mgr.p_band_id.set_quiet(5);
    mgr.init_load();
}

// ---- dump basics -----------------------------------------------------------

TEST_CASE("Frame::dump produces valid CI-V frame from parsed input", "[cat]") {
    auto raw = ci_v_frame(C_RD_FREQ, {});
    Frame req(raw.data(), raw.size());
    auto buf = req.dump();

    REQUIRE(buf.size() == 6); // prefix(2) + dst + src + cmd + suffix
    REQUIRE(u8(buf, 0) == FRAME_PRE);
    REQUIRE(u8(buf, 1) == FRAME_PRE);
    REQUIRE(u8(buf, 2) == 0xE0);
    REQUIRE(u8(buf, 3) == LOCAL_ADDRESS);
    REQUIRE(u8(buf, 4) == C_RD_FREQ);
    REQUIRE(u8(buf, 5) == FRAME_END);
}

// ---- unknown command -------------------------------------------------------

TEST_CASE("Frame::process returns CODE_NG for unknown command", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    auto req = frame_from(0xFF);
    auto resp = req.process();
    auto buf = resp.dump();

    // set_unsupported → command = CODE_NG
    REQUIRE(u8(buf, 4) == CODE_NG);
}

// ---- C_RD_FREQ (0x03) ------------------------------------------------------

TEST_CASE("C_RD_FREQ reads current frequency as 5-byte BCD", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    // cp_fg_freq = p_band_vfoa_freq = 7.1 MHz
    REQUIRE(mgr.cp_fg_freq.get() == 7'100'000);

    auto req = frame_from(C_RD_FREQ);
    auto resp = req.process();
    auto buf = resp.dump();

    REQUIRE(buf.size() == 11); // frame(6) + BCD payload(5)
    // 7.1 MHz (7100000) little-endian 10-digit BCD → 00 00 10 07 00
    // 7100000 / 1000 = 7100 → hundreds 0, tens 1 at index 2...
    // to_bcd: bcd[2] = 0x10 (digits "10" of "7100"), bcd[3] = 0x07
    REQUIRE(u8(buf, 5) == 0x00);
    REQUIRE(u8(buf, 6) == 0x00);
    REQUIRE(u8(buf, 7) == 0x10);
    REQUIRE(u8(buf, 8) == 0x07);
    REQUIRE(u8(buf, 9) == 0x00);
}

// ---- C_SET_FREQ (0x05) ------------------------------------------------------

TEST_CASE("C_SET_FREQ sets frequency from 5-byte BCD", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    // 14.2 MHz in little-endian BCD: to_bcd(bcd, 14200000, 10)
    // 14200000 / 1000 = 14200 → digits: ... 00 20 14 → bcd[2]=0x20, bcd[3]=0x14
    std::vector<uint8_t> payload = {0x00, 0x00, 0x20, 0x14, 0x00};

    auto req = frame_from(C_SET_FREQ, payload);
    auto resp = req.process();
    auto buf = resp.dump();

    REQUIRE(u8(buf, 4) == CODE_OK);
    REQUIRE(mgr.cp_fg_freq.get() == 14'200'000);
}

// ---- C_RD_MODE (0x04) ------------------------------------------------------

TEST_CASE("C_RD_MODE reads current mode as 2 bytes", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    auto req = frame_from(C_RD_MODE);
    auto resp = req.process();
    auto buf = resp.dump();

    // band 5 VFO A mode = usb → M_USB = 0x01
    REQUIRE(buf.size() == 8); // frame(6) + mode(2)
    REQUIRE(u8(buf, 5) == M_USB);
    REQUIRE(u8(buf, 6) == M_USB);
}

// ---- C_SET_MODE (0x06) ------------------------------------------------------

TEST_CASE("C_SET_MODE sets mode", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    // Set CW (0x03)
    auto req = frame_from(C_SET_MODE, {0x03});
    auto resp = req.process();

    REQUIRE(u8(resp.dump(), 4) == CODE_OK);
    REQUIRE(mgr.cp_cur_mode.get() == x6100_mode_cw);
}

// ---- C_SET_VFO (0x07) ------------------------------------------------------

TEST_CASE("C_SET_VFO switches VFO", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    // Read current VFO (empty payload)
    {
        auto req = frame_from(C_SET_VFO, {});
        auto resp = req.process();
        auto buf = resp.dump();
        REQUIRE(buf.size() == 7);
        REQUIRE(u8(buf, 5) == S_VFOA); // current VFO = A
    }

    // Switch to B
    {
        auto req = frame_from(C_SET_VFO, {S_VFOB});
        auto resp = req.process();
        REQUIRE(u8(resp.dump(), 4) == CODE_OK);
        REQUIRE(mgr.p_band_current_vfo.get() == X6100_VFO_B);
    }

    // Switch back to A
    {
        auto req = frame_from(C_SET_VFO, {S_VFOA});
        auto resp = req.process();
        REQUIRE(u8(resp.dump(), 4) == CODE_OK);
        REQUIRE(mgr.p_band_current_vfo.get() == X6100_VFO_A);
    }
}

TEST_CASE("C_SET_VFO S_XCHNG swaps VFO", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    REQUIRE(mgr.p_band_current_vfo.get() == X6100_VFO_A);

    auto req = frame_from(C_SET_VFO, {S_XCHNG});
    auto resp = req.process();
    REQUIRE(u8(resp.dump(), 4) == CODE_OK);
    REQUIRE(mgr.p_band_current_vfo.get() == X6100_VFO_B);
}

// ---- C_CTL_SPLT (0x0F) ------------------------------------------------------

TEST_CASE("C_CTL_SPLT read split status", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    mgr.p_band_split.set(0);
    {
        auto req = frame_from(C_CTL_SPLT, {});
        auto resp = req.process();
        auto buf = resp.dump();
        REQUIRE(buf.size() == 7);
        REQUIRE(u8(buf, 5) == 0);
    }

    mgr.p_band_split.set(1);
    {
        auto req = frame_from(C_CTL_SPLT, {});
        auto resp = req.process();
        auto buf = resp.dump();
        REQUIRE(u8(buf, 5) == 1);
    }
}

// ---- C_SET_TS (0x10) -------------------------------------------------------

TEST_CASE("C_SET_TS read/write tuning step", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    // Read default (500 Hz → 0x02)
    {
        auto req = frame_from(C_SET_TS, {});
        auto resp = req.process();
        auto buf = resp.dump();
        REQUIRE(buf.size() == 7);
        REQUIRE(u8(buf, 5) == 0x02);
    }

    // Write 100 Hz (0x01)
    {
        auto req = frame_from(C_SET_TS, {0x01});
        auto resp = req.process();
        REQUIRE(u8(resp.dump(), 4) == CODE_OK);
        REQUIRE(mgr.p_mode_freq_step.get() == 100);
    }
}

// ---- C_CTL_ATT (0x11) ------------------------------------------------------

TEST_CASE("C_CTL_ATT read/write attenuator", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    // Read default
    {
        auto req = frame_from(C_CTL_ATT, {});
        auto resp = req.process();
        auto buf = resp.dump();
        // cp_cur_att default 0 → att * 0x20 = 0
        REQUIRE(buf.size() == 7);
        REQUIRE(u8(buf, 5) == 0x00);
    }

    // Read when enabled: att=1 → 0x20
    {
        mgr.cp_cur_att.set(1);
        auto req = frame_from(C_CTL_ATT, {});
        auto resp = req.process();
        auto buf = resp.dump();
        REQUIRE(u8(buf, 5) == 0x20);
    }
}

// ---- C_CTL_LVL (0x14) ------------------------------------------------------

TEST_CASE("C_CTL_LVL volume read", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    mgr.p_volume.set(30);

    // Read: sub 0x01 → {sub, bcd1, bcd2}
    auto req = frame_from(C_CTL_LVL, {0x01});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 9);
    REQUIRE(u8(buf, 5) == 0x01);
}

TEST_CASE("C_CTL_LVL RF gain read", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    mgr.p_rfgain.set(50);

    auto req = frame_from(C_CTL_LVL, {0x02});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 9);
    REQUIRE(u8(buf, 5) == 0x02);
}

TEST_CASE("C_CTL_LVL squelch read", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    mgr.p_squelch.set(10);

    auto req = frame_from(C_CTL_LVL, {0x03});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 9);
    REQUIRE(u8(buf, 5) == 0x03);
}

// ---- C_RD_SQSM (0x15) ------------------------------------------------------

TEST_CASE("C_RD_SQSM s-meter returns 3 bytes with stub meter == 0", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    auto req = frame_from(C_RD_SQSM, {0x02});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 9);
    REQUIRE(u8(buf, 5) == 0x02);
    // meter stub returns 0 → val = 0*0.75 + 96 = 96 → 3-digit BE BCD = 0x96
    REQUIRE(u8(buf, 6) == 0x00);
    REQUIRE(u8(buf, 7) == 0x96);
}

// ---- C_CTL_FUNC (0x16) -----------------------------------------------------

TEST_CASE("C_CTL_FUNC preamp read", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    mgr.cp_cur_pre.set(1);

    auto req = frame_from(C_CTL_FUNC, {0x02});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 8);
    REQUIRE(u8(buf, 5) == 0x02);
    REQUIRE(u8(buf, 6) == 1);
}

// ---- C_RD_TRXID (0x19) -----------------------------------------------------

TEST_CASE("C_RD_TRXID returns transceiver ID", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    auto req = frame_from(C_RD_TRXID, {0x00});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 8);
    REQUIRE(u8(buf, 5) == 0x00);
    REQUIRE(u8(buf, 6) == LOCAL_ADDRESS);
}

// ---- C_CTL_MEM (0x1A) ------------------------------------------------------

TEST_CASE("C_CTL_MEM MEM_DM_FG read returns data mode info", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    auto req = frame_from(C_CTL_MEM, {MEM_DM_FG});
    auto resp = req.process();
    auto buf = resp.dump();
    // {sub, mode, data_mode=0, 0x00} = 4 bytes → frame 6 + 4 = 10
    REQUIRE(buf.size() == 10);
    REQUIRE(u8(buf, 5) == MEM_DM_FG);
    REQUIRE(u8(buf, 6) == M_USB);
    REQUIRE(u8(buf, 7) == 0x00); // data_mode = false
}

// ---- C_CTL_PTT (0x1C) ------------------------------------------------------

TEST_CASE("C_CTL_PTT read returns RX state from stub", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    auto req = frame_from(C_CTL_PTT, {0x00});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 8);
    REQUIRE(u8(buf, 5) == 0x00);
    REQUIRE(u8(buf, 6) == 0x00); // RADIO_RX → 0
}

// ---- C_SEND_SEL_FREQ (0x25) ------------------------------------------------

TEST_CASE("C_SEND_SEL_FREQ main/sub freq read", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    // Main VFO (sub 0) → cp_fg_freq = vfoa_freq = 7.1 MHz.
    // process() pre-copies the 1-byte request payload (sub selector) into the
    // response, then the handler appends {sub, 5×BCD} = 6 more bytes.
    auto req = frame_from(C_SEND_SEL_FREQ, {0x00});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 13); // frame(6) + selector(1) + appended 6
    REQUIRE(u8(buf, 5) == 0x00);
    // appended {0x00, bcd[1..5]} → BCD of 7.1 MHz at buf[7..11]
    REQUIRE(u8(buf, 7) == 0x00);
    REQUIRE(u8(buf, 8) == 0x00);
    REQUIRE(u8(buf, 9) == 0x10);
    REQUIRE(u8(buf, 10) == 0x07);
    REQUIRE(u8(buf, 11) == 0x00);
}

// ---- C_SEND_SEL_MODE (0x26) ------------------------------------------------

TEST_CASE("C_SEND_SEL_MODE main/sub mode read", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    auto req = frame_from(C_SEND_SEL_MODE, {0x00});
    auto resp = req.process();
    auto buf = resp.dump();
    // {sub, mode, data_mode, 0x01} = 4 bytes
    REQUIRE(buf.size() == 10);
    REQUIRE(u8(buf, 5) == 0x00);
    REQUIRE(u8(buf, 6) == M_USB);
}

// ---- C_CTL_SCP (0x27) ------------------------------------------------------

TEST_CASE("C_CTL_SCP sub 0x10 returns scope available", "[cat]") {
    TestDbGuard db;
    SettingsManager mgr;
    init_band_5(db, mgr);
    cat_frame_set_sm(&mgr);

    auto req = frame_from(C_CTL_SCP, {0x10});
    auto resp = req.process();
    auto buf = resp.dump();
    REQUIRE(buf.size() == 8);
    REQUIRE(u8(buf, 5) == 0x10);
    REQUIRE(u8(buf, 6) == 0x01);
}

// ---- cleanup ---------------------------------------------------------------

TEST_CASE("cat_frame_set_sm(nullptr) restores production global", "[cat]") {
    cat_frame_set_sm(nullptr);
    SUCCEED();
}
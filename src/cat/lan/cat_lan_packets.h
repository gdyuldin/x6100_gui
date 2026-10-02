#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PURGE_SECONDS          10
#define TOKEN_RENEWAL          60000
#define PING_PERIOD            500
#define IDLE_PERIOD            100
#define AREYOUTHERE_PERIOD     500
#define WATCHDOG_PERIOD        500
#define RETRANSMIT_PERIOD      100
#define BUFSIZE                500
#define MAX_MISSING            50

#define CONTROL_SIZE           0x10
#define WATCHDOG_SIZE          0x14
#define PING_SIZE              0x15
#define OPENCLOSE_SIZE         0x16

// Control packet types
#define CTL_TYPE_CLOSE      0x05   // Close/disconnect

// Open/close magic values
#define MAGIC_OPEN          0x04   // Open handshake
#define MAGIC_CLOSE         0x05   // Close/disconnect

// Ping timeout for connection staleness detection
#define PING_TIMEOUT_SEC    15

#define RETRANSMIT_RANGE_SIZE  0x18
#define TOKEN_SIZE             0x40
#define STATUS_SIZE            0x50
#define LOGIN_RESPONSE_SIZE    0x60
#define LOGIN_SIZE             0x80
#define CONNINFO_SIZE          0x90
#define CAPABILITIES_SIZE      0x42
#define RADIO_CAP_SIZE         0x66
#define CIV_SIZE               0x15
#define AUDIO_PORT             50003
#define AUDIO_SIZE             0x18
#define DATA_SIZE              0x15
#define GUIDLEN                16

#pragma pack(push, 1)

typedef struct control_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
} control_packet_t;

typedef struct ping_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint8_t  reply;
    union {
        struct {
            uint32_t time;
        };
        struct {
            uint16_t datalen;
            uint16_t sendseq;
        };
    };
} ping_packet_t;

typedef struct openclose_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint16_t data;
    uint8_t  unused;
    uint16_t sendseq;
    uint8_t  magic;
} openclose_packet_t;

typedef struct audio_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint16_t ident;
    uint16_t sendseq;
    uint16_t unused;
    uint16_t datalen;
} audio_packet_t;

typedef struct token_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint32_t payloadsize;
    uint8_t  requestreply;
    uint8_t  requesttype;
    uint16_t innerseq;
    uint8_t  unusedb[2];
    uint16_t tokrequest;
    uint32_t token;
    union {
        struct {
            uint16_t authstartid;
            uint8_t  unusedg2[2];
            uint16_t resetcap;
            uint8_t  unusedg1;
            uint16_t commoncap;
            uint8_t  unusedh;
            uint8_t  macaddress[6];
        };
        uint8_t guid[GUIDLEN];
    };
    uint32_t response;
    uint8_t  unusede[12];
} token_packet_t;

typedef struct status_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint32_t payloadsize;
    uint8_t  requestreply;
    uint8_t  requesttype;
    uint16_t innerseq;
    uint8_t  unusedb[2];
    uint16_t tokrequest;
    uint32_t token;
    union {
        struct {
            uint16_t authstartid;
            uint8_t  unusedd[5];
            uint16_t commoncap;
            uint8_t  unusede;
            uint8_t  macaddress[6];
        };
        uint8_t guid[GUIDLEN];
    };
    uint32_t error;
    uint8_t  unusedg[12];
    uint8_t  disc;
    uint8_t  unusedh;
    uint16_t civport;
    uint16_t unusedi;
    uint16_t audioport;
    uint8_t  unusedj[8];
} status_packet_t;

typedef struct login_response_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint32_t payloadsize;
    uint8_t  requestreply;
    uint8_t  requesttype;
    uint16_t innerseq;
    uint8_t  unusedb[2];
    uint16_t tokrequest;
    uint32_t token;
    uint16_t authstartid;
    uint8_t  unusedd[14];
    uint32_t error;
    uint8_t  unusede[12];
    char     connection[16];
    uint8_t  unusedf[16];
} login_response_packet_t;

typedef struct login_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint32_t payloadsize;
    uint8_t  requestreply;
    uint8_t  requesttype;
    uint16_t innerseq;
    uint8_t  unusedb[2];
    uint16_t tokrequest;
    uint32_t token;
    uint8_t  unusedc[32];
    char     username[16];
    char     password[16];
    char     name[16];
    uint8_t  unusedf[16];
} login_packet_t;

typedef struct conninfo_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint32_t payloadsize;
    uint8_t  requestreply;
    uint8_t  requesttype;
    uint16_t innerseq;
    uint8_t  unusedb[2];
    uint16_t tokrequest;
    uint32_t token;
    union {
        struct {
            uint16_t authstartid;
            uint8_t  unusedg[5];
            uint16_t commoncap;
            uint8_t  unusedh;
            uint8_t  macaddress[6];
        };
        uint8_t guid[GUIDLEN];
    };
    uint8_t  unusedab[16];
    char     name[32];
    union {
        struct {
            uint32_t busy;
            char     computer[16];
            uint8_t  unusedi[16];
            uint32_t ipaddress;
            uint8_t  unusedj[8];
        };
        struct {
            char     username[16];
            uint8_t  rxenable;
            uint8_t  txenable;
            uint8_t  rxcodec;
            uint8_t  txcodec;
            uint32_t rxsample;
            uint32_t txsample;
            uint32_t civport;
            uint32_t audioport;
            uint32_t txbuffer;
            uint8_t  convert;
            uint8_t  unusedl[7];
        };
    };
} conninfo_packet_t;

typedef struct radio_cap_packet {
    union {
        struct {
            uint8_t  unusede[7];
            uint16_t commoncap;
            uint8_t  unused;
            uint8_t  macaddress[6];
        };
        uint8_t guid[GUIDLEN];
    };
    char     name[32];
    char     audio[32];
    uint16_t conntype;
    uint8_t  civ;
    uint16_t rxsample;
    uint16_t txsample;
    uint8_t  enablea;
    uint8_t  enableb;
    uint8_t  enablec;
    uint32_t baudrate;
    uint16_t capf;
    uint8_t  unusedi;
    uint16_t capg;
    uint8_t  unusedj[3];
} radio_cap_packet_t;

typedef struct capabilities_packet {
    uint32_t len;
    uint16_t type;
    uint16_t seq;
    uint32_t sentid;
    uint32_t rcvdid;
    uint32_t payloadsize;
    uint8_t  requestreply;
    uint8_t  requesttype;
    uint16_t innerseq;
    uint8_t  unusedb[2];
    uint16_t tokrequest;
    uint32_t token;
    uint8_t  unusedd[32];
    uint16_t numradios;
} capabilities_packet_t;

#pragma pack(pop)

static inline void passcode_encode(const char *in, int in_len, uint8_t *out, int *out_len) {
    static const uint8_t sequence[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0x47,0x5d,0x4c,0x42,0x66,0x20,0x23,0x46,0x4e,0x57,0x45,0x3d,0x67,0x76,0x60,0x41,0x62,0x39,0x59,0x2d,0x68,0x7e,
        0x7c,0x65,0x7d,0x49,0x29,0x72,0x73,0x78,0x21,0x6e,0x5a,0x5e,0x4a,0x3e,0x71,0x2c,0x2a,0x54,0x3c,0x3a,0x63,0x4f,
        0x43,0x75,0x27,0x79,0x5b,0x35,0x70,0x48,0x6b,0x56,0x6f,0x34,0x32,0x6c,0x30,0x61,0x6d,0x7b,0x2f,0x4b,0x64,0x38,
        0x2b,0x2e,0x50,0x40,0x3f,0x55,0x33,0x37,0x25,0x77,0x24,0x26,0x74,0x6a,0x28,0x53,0x4d,0x69,0x22,0x5c,0x44,0x31,
        0x36,0x58,0x3b,0x7a,0x51,0x5f,0x52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    int i;
    for (i = 0; i < in_len && i < 16; i++) {
        int p = (uint8_t)in[i] + i;
        if (p > 126) {
            p = 32 + p % 127;
        }
        out[i] = sequence[p];
    }
    *out_len = i;
}

#ifdef __cplusplus
}
#endif

#pragma once
// ============================================================================
// GVRETServer.h
// GVRET-compatible TCP server on port 23 — makes M5Dial appear as a SavvyCAN
// network connection device.
//
// GVRET binary protocol (from collin80/M2RET CommProtocol.txt):
//
//   All binary commands start with 0xF1, followed by a command byte.
//   SavvyCAN sends 0xF1 0xE7 on connect to request binary mode.
//
//   INCOMING CAN FRAME (device → SavvyCAN):
//     [0]    0xF1        frame start
//     [1]    0x00        command: CAN frame
//     [2-5]  timestamp   microseconds, little-endian uint32
//     [6-9]  can_id      little-endian uint32 (bit 31 set = extended frame)
//     [10]   dlc_bus     bits 0-3: data length, bits 4-7: bus number (0)
//     [11..] data        0-8 bytes
//     [last] 0x00        checksum placeholder (unused, always 0)
//
//   KEY COMMANDS (SavvyCAN → device):
//     0xF1 0xE7          → enable binary mode, device responds 0xF1 0xE7
//     0xF1 0x01          → get device info, device responds with version string
//     0xF1 0x07 [speed4] → set CAN bus speed (ignored — already set)
//     0xF1 0x09          → enable CAN bus, device responds 0xF1 0x09
//     0xF1 0x0A          → disable CAN bus, device responds 0xF1 0x0A
//     0xF1 0x14 [4 bytes] → transmit CAN frame request
//
// Port: 23 (standard GVRET/telnet port expected by SavvyCAN)
// Max clients: 4 simultaneous SavvyCAN connections
// Frame buffer: up to 32 frames batched per send cycle (every 10ms)
// ============================================================================

#include <Arduino.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "driver/twai.h"

#define GVRET_PORT          23
#define GVRET_MAX_CLIENTS   4
#define GVRET_FLUSH_MS      10      // flush frame buffer every 10ms
#define GVRET_FRAME_BUF_SZ  512     // bytes — holds ~35 CAN frames

// GVRET command bytes
#define GVRET_CMD_FRAME         0x00
#define GVRET_CMD_GET_INFO      0x01  // time sync — echo back with current timestamp
#define GVRET_CMD_GET_EXT_BUSES 0x06  // get extended bus info (SWCAN/LIN)
#define GVRET_CMD_GET_DEV_INFO  0x07  // get device info (baud rates, build number)
#define GVRET_CMD_BUS_ENABLE    0x09  // comm validation — must echo back to go Connected
#define GVRET_CMD_BUS_DISABLE   0x0A  // bus disable
#define GVRET_CMD_GET_NUM_BUSES 0x0C  // get number of implemented buses
#define GVRET_CMD_TRANSMIT      0x14  // transmit a CAN frame
#define GVRET_CMD_BINARY_MODE   0xE7  // negotiate binary mode
#define GVRET_FRAME_START       0xF1

class GVRETServer {
public:
    static GVRETServer& getInstance() {
        static GVRETServer inst;
        return inst;
    }

    void begin();
    void stop();

    // Call from main loop every iteration — accepts new clients,
    // processes incoming commands, flushes frame buffer
    void update();

    // Push a received CAN frame to all connected SavvyCAN clients
    void pushFrame(uint32_t id, bool extended, const uint8_t* data, uint8_t dlc);

    bool hasClients() const { return _clientCount > 0; }

private:
    GVRETServer() : _server(GVRET_PORT), _clientCount(0), _lastFlush(0), _bufLen(0), _running(false) {}
    GVRETServer(const GVRETServer&) = delete;
    GVRETServer& operator=(const GVRETServer&) = delete;

    WiFiServer      _server;
    WiFiClient      _clients[GVRET_MAX_CLIENTS];
    int             _clientCount;
    uint32_t        _lastFlush;
    bool            _running;

    // Outgoing frame buffer — batched for efficiency
    uint8_t         _frameBuf[GVRET_FRAME_BUF_SZ];
    int             _bufLen;

    void _acceptClients();
    void _processClientInput(int idx);
    void _handleCommand(int clientIdx, uint8_t cmd, const uint8_t* payload, int payloadLen);
    void _flushFrameBuffer();
    void _sendResponse(int clientIdx, uint8_t cmd, const uint8_t* data, int len);
    void _appendFrame(uint32_t id, bool extended, const uint8_t* data, uint8_t dlc);
};

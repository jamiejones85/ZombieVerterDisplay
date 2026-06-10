// ============================================================================
// GVRETServer.cpp
// ============================================================================

#include "GVRETServer.h"

// ---------------------------------------------------------------------------
// begin / stop
// ---------------------------------------------------------------------------
void GVRETServer::begin() {
    _server.begin();
    _server.setNoDelay(true);
    _running = true;
    _bufLen  = 0;
    Serial.printf("[GVRET] TCP server listening on port %d\n", GVRET_PORT);
}

void GVRETServer::stop() {
    for (int i = 0; i < GVRET_MAX_CLIENTS; i++) {
        if (_clients[i].connected()) _clients[i].stop();
    }
    _server.end();
    _running     = false;
    _clientCount = 0;
    _bufLen      = 0;
    Serial.println("[GVRET] TCP server stopped");
}

// ---------------------------------------------------------------------------
// update — call every loop()
// ---------------------------------------------------------------------------
void GVRETServer::update() {
    if (!_running) return;
    _acceptClients();
    for (int i = 0; i < GVRET_MAX_CLIENTS; i++) {
        if (_clients[i].connected() && _clients[i].available()) {
            _processClientInput(i);
        }
    }
    if (millis() - _lastFlush >= GVRET_FLUSH_MS) {
        _flushFrameBuffer();
        _lastFlush = millis();
    }
}

// ---------------------------------------------------------------------------
// pushFrame — called from main loop when a CAN frame is received
// ---------------------------------------------------------------------------
void GVRETServer::pushFrame(uint32_t id, bool extended, const uint8_t* data, uint8_t dlc) {
    if (!_running) return;
    _appendFrame(id, extended, data, dlc);
}

// ---------------------------------------------------------------------------
// _acceptClients
// ---------------------------------------------------------------------------
void GVRETServer::_acceptClients() {
    if (!_server.hasClient()) return;

    // Find a free slot
    for (int i = 0; i < GVRET_MAX_CLIENTS; i++) {
        if (!_clients[i].connected()) {
            _clients[i] = _server.available();
            _clients[i].setNoDelay(true);
            _clientCount++;
            Serial.printf("[GVRET] Client %d connected from %s\n",
                          i, _clients[i].remoteIP().toString().c_str());
            return;
        }
    }

    // No free slot — reject
    WiFiClient rejected = _server.available();
    rejected.stop();
    Serial.println("[GVRET] Max clients reached — connection rejected");
}

// ---------------------------------------------------------------------------
// _processClientInput — read and handle incoming GVRET commands
// Maintains per-client parse state using a simple static approach:
// GVRET is low-bandwidth on the command side so we read byte-by-byte here.
// ---------------------------------------------------------------------------
void GVRETServer::_processClientInput(int idx) {
    WiFiClient& client = _clients[idx];

    // Static per-client parse state
    static uint8_t  state[GVRET_MAX_CLIENTS]       = {};  // 0=idle, 1=got_F1, 2+=collecting
    static uint8_t  cmdByte[GVRET_MAX_CLIENTS]      = {};
    static uint8_t  payload[GVRET_MAX_CLIENTS][16]  = {};
    static int      payloadIdx[GVRET_MAX_CLIENTS]   = {};
    static int      payloadLen[GVRET_MAX_CLIENTS]   = {};

    // Check if client disconnected
    if (!client.connected()) {
        state[idx] = 0;
        Serial.printf("[GVRET] Client %d disconnected\n", idx);
        return;
    }

    while (client.available()) {
        uint8_t b = client.read();

        switch (state[idx]) {
            case 0: // idle — wait for 0xF1
                if (b == GVRET_FRAME_START) state[idx] = 1;
                break;

            case 1: // got 0xF1 — next byte is command
                cmdByte[idx]    = b;
                payloadIdx[idx] = 0;

                // Determine expected payload length for each command
                switch (b) {
                    case GVRET_CMD_BINARY_MODE:   payloadLen[idx] = 0; break;
                    case GVRET_CMD_GET_INFO:       payloadLen[idx] = 0; break;
                    case GVRET_CMD_GET_EXT_BUSES:  payloadLen[idx] = 0; break;
                    case GVRET_CMD_GET_DEV_INFO:   payloadLen[idx] = 0; break;
                    case GVRET_CMD_BUS_ENABLE:     payloadLen[idx] = 0; break;
                    case GVRET_CMD_BUS_DISABLE:    payloadLen[idx] = 0; break;
                    case GVRET_CMD_GET_NUM_BUSES:  payloadLen[idx] = 0; break;
                    case GVRET_CMD_TRANSMIT:       payloadLen[idx] = 14; break;
                    default:                       payloadLen[idx] = 0; break;
                }

                if (payloadLen[idx] == 0) {
                    _handleCommand(idx, cmdByte[idx], nullptr, 0);
                    state[idx] = 0;
                } else {
                    state[idx] = 2;
                }
                break;

            case 2: // collecting payload bytes
                if (payloadIdx[idx] < 16) {
                    payload[idx][payloadIdx[idx]++] = b;
                }
                if (payloadIdx[idx] >= payloadLen[idx]) {
                    _handleCommand(idx, cmdByte[idx], payload[idx], payloadLen[idx]);
                    state[idx] = 0;
                }
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// _handleCommand — respond to SavvyCAN commands
// ---------------------------------------------------------------------------
void GVRETServer::_handleCommand(int clientIdx, uint8_t cmd,
                                  const uint8_t* payload, int payloadLen) {
    switch (cmd) {

        case GVRET_CMD_BINARY_MODE:
            // Echo back to confirm binary mode
            _sendResponse(clientIdx, GVRET_CMD_BINARY_MODE, nullptr, 0);
            Serial.printf("[GVRET] Client %d: binary mode\n", clientIdx);
            break;

        case GVRET_CMD_GET_INFO: {
            // 0x01 = time sync. Response: 0xF1 0x01 + uint32 timestamp (micros)
            // SavvyCAN uses this to calibrate its time base.
            uint32_t ts = (uint32_t)micros();
            uint8_t resp[4];
            resp[0] = (ts)       & 0xFF;
            resp[1] = (ts >>  8) & 0xFF;
            resp[2] = (ts >> 16) & 0xFF;
            resp[3] = (ts >> 24) & 0xFF;
            _sendResponse(clientIdx, GVRET_CMD_GET_INFO, resp, 4);
            break;
        }

        case GVRET_CMD_GET_NUM_BUSES: {
            // 0x0C = get number of actually implemented buses.
            // Response: 0xF1 0x0C + uint8 count (1 = one CAN bus)
            // SavvyCAN logs "Got num buses reply, Get number of buses = N"
            uint8_t resp[1] = { 1 };
            _sendResponse(clientIdx, GVRET_CMD_GET_NUM_BUSES, resp, 1);
            Serial.printf("[GVRET] Client %d: get num buses -> 1\n", clientIdx);
            break;
        }

        case GVRET_CMD_GET_DEV_INFO: {
            // 0x07 = get device info. SavvyCAN parses:
            //   uint32 build_number
            //   uint32 can0_baud
            //   uint32 can1_baud  (0 = not present)
            //   uint8  single_wire_support
            // Total: 13 bytes
            uint8_t resp[13] = {};
            uint32_t build = 333;        // pretend to be build 333 — well-known working version
            uint32_t baud0 = 500000;     // CAN0 = 500kbps
            uint32_t baud1 = 0;          // no second bus
            resp[0]  = (build)       & 0xFF;
            resp[1]  = (build >>  8) & 0xFF;
            resp[2]  = (build >> 16) & 0xFF;
            resp[3]  = (build >> 24) & 0xFF;
            resp[4]  = (baud0)       & 0xFF;
            resp[5]  = (baud0 >>  8) & 0xFF;
            resp[6]  = (baud0 >> 16) & 0xFF;
            resp[7]  = (baud0 >> 24) & 0xFF;
            resp[8]  = (baud1)       & 0xFF;
            resp[9]  = (baud1 >>  8) & 0xFF;
            resp[10] = (baud1 >> 16) & 0xFF;
            resp[11] = (baud1 >> 24) & 0xFF;
            resp[12] = 0;                // no single-wire CAN
            _sendResponse(clientIdx, GVRET_CMD_GET_DEV_INFO, resp, 13);
            Serial.printf("[GVRET] Client %d: get device info\n", clientIdx);
            break;
        }

        case GVRET_CMD_GET_EXT_BUSES: {
            // 0x06 = get extended bus info (SWCAN, LIN buses).
            // Response: 0xF1 0x06 + uint32 swcan_baud + uint32 lin1_baud + uint32 lin2_baud
            // All zero = not present.
            uint8_t resp[12] = {};
            _sendResponse(clientIdx, GVRET_CMD_GET_EXT_BUSES, resp, 12);
            break;
        }

        case GVRET_CMD_BUS_ENABLE:
            // 0x09 = comm validation / bus enable.
            // THIS is what SavvyCAN waits for to set status = Connected.
            // Must echo back promptly.
            _sendResponse(clientIdx, GVRET_CMD_BUS_ENABLE, nullptr, 0);
            Serial.printf("[GVRET] Client %d: bus enable — now Connected\n", clientIdx);
            break;

        case GVRET_CMD_BUS_DISABLE:
            _sendResponse(clientIdx, GVRET_CMD_BUS_DISABLE, nullptr, 0);
            Serial.printf("[GVRET] Client %d: bus disable\n", clientIdx);
            break;

        case GVRET_CMD_TRANSMIT:
            // SavvyCAN wants to transmit a frame onto the bus
            // payload: id(4) + dlc_bus(1) + data(8) + checksum(1)
            if (payloadLen >= 5) {
                uint32_t id;
                memcpy(&id, payload, 4);
                uint8_t dlc = payload[4] & 0x0F;
                bool    ext = (id & 0x80000000) != 0;
                id &= 0x1FFFFFFF;  // strip extended flag

                twai_message_t msg;
                msg.identifier         = id;
                msg.extd               = ext ? 1 : 0;
                msg.rtr                = 0;
                msg.data_length_code   = dlc > 8 ? 8 : dlc;
                for (int i = 0; i < msg.data_length_code; i++) {
                    msg.data[i] = (i + 5 < payloadLen) ? payload[i + 5] : 0;
                }
                if (twai_transmit(&msg, pdMS_TO_TICKS(5)) == ESP_OK) {
                    Serial.printf("[GVRET] Client %d: TX 0x%03X dlc=%d\n",
                                  clientIdx, id, dlc);
                } else {
                    Serial.printf("[GVRET] Client %d: TX 0x%03X failed\n",
                                  clientIdx, id);
                }
            }
            break;

        default:
            // Unknown command — ignore silently
            break;
    }
}

// ---------------------------------------------------------------------------
// _sendResponse — send 0xF1 + cmd + data to one client
// ---------------------------------------------------------------------------
void GVRETServer::_sendResponse(int clientIdx, uint8_t cmd,
                                 const uint8_t* data, int len) {
    if (!_clients[clientIdx].connected()) return;
    uint8_t hdr[2] = { GVRET_FRAME_START, cmd };
    _clients[clientIdx].write(hdr, 2);
    if (data && len > 0) {
        _clients[clientIdx].write(data, len);
    }
}

// ---------------------------------------------------------------------------
// _appendFrame — encode a CAN frame into the outgoing buffer
//
// Frame wire format (14 bytes for DLC=8):
//   [0]    0xF1          frame start
//   [1]    0x00          command: incoming frame
//   [2-5]  micros()      timestamp little-endian uint32
//   [6-9]  can_id        little-endian uint32, bit31 set if extended
//   [10]   (bus<<4)|dlc  upper nibble = bus (0), lower nibble = dlc
//   [11..] data          DLC bytes
//   [last] 0x00          checksum (unused)
// ---------------------------------------------------------------------------
void GVRETServer::_appendFrame(uint32_t id, bool extended,
                                const uint8_t* data, uint8_t dlc) {
    if (dlc > 8) dlc = 8;
    int frameSize = 11 + dlc + 1;  // header(2) + ts(4) + id(4) + dlc_bus(1) + data + crc(1)

    if (_bufLen + frameSize > GVRET_FRAME_BUF_SZ) {
        // Buffer full — flush now before appending
        _flushFrameBuffer();
    }

    uint32_t ts = (uint32_t)micros();
    uint32_t wireId = id;
    if (extended) wireId |= 0x80000000;

    uint8_t* p = _frameBuf + _bufLen;
    *p++ = GVRET_FRAME_START;
    *p++ = GVRET_CMD_FRAME;
    // Timestamp little-endian
    *p++ = (ts)       & 0xFF;
    *p++ = (ts >>  8) & 0xFF;
    *p++ = (ts >> 16) & 0xFF;
    *p++ = (ts >> 24) & 0xFF;
    // CAN ID little-endian
    *p++ = (wireId)       & 0xFF;
    *p++ = (wireId >>  8) & 0xFF;
    *p++ = (wireId >> 16) & 0xFF;
    *p++ = (wireId >> 24) & 0xFF;
    // DLC + bus number: upper nibble = bus (0), lower nibble = dlc
    // Bus 0: (0 << 4) | dlc = dlc
    *p++ = (uint8_t)((0 << 4) | (dlc & 0x0F));
    // Data
    for (int i = 0; i < dlc; i++) *p++ = data[i];
    // Checksum placeholder
    *p++ = 0x00;

    _bufLen += frameSize;
}

// ---------------------------------------------------------------------------
// _flushFrameBuffer — send buffered frames to all connected clients
// ---------------------------------------------------------------------------
void GVRETServer::_flushFrameBuffer() {
    // Recount live clients on every flush — avoids counter drift bugs
    int liveCount = 0;
    for (int i = 0; i < GVRET_MAX_CLIENTS; i++) {
        if (_clients[i].connected()) liveCount++;
    }
    _clientCount = liveCount;

    if (_bufLen == 0 || _clientCount == 0) {
        _bufLen = 0;
        return;
    }

    for (int i = 0; i < GVRET_MAX_CLIENTS; i++) {
        if (_clients[i].connected()) {
            _clients[i].write(_frameBuf, _bufLen);
        }
    }
    _bufLen = 0;
}

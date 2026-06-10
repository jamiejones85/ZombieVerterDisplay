#include "SerialCommandHandler.h"

SerialCommandHandler::SerialCommandHandler(CanSDO &canSDO)
    : canSDO(canSDO), serialPort(1), lastCommandTime(0), timeoutCommandsSent(false), enabled(true),
      lastTimeoutTime(0), lastCommandId(-1), lastCommandIdTime(0) {  // Use Serial1
    buffer.reserve(64);  // Reserve space for buffer
}

void SerialCommandHandler::Setup() {
    // Initialize serial port on pins 40 (RX) and 38 (TX)
    serialPort.begin(SERIAL_CMD_BAUD, SERIAL_8N1, SERIAL_CMD_RX_PIN, SERIAL_CMD_TX_PIN);
    Serial.println("Serial command handler initialized on pins 40/38");
    Serial.print("Serial relay initial state: ");
    Serial.println(enabled ? "ENABLED" : "DISABLED");
    lastCommandTime = millis();  // Initialize timestamp
}

void SerialCommandHandler::Loop() {
    // Skip if serial relay is disabled
    if (!enabled) {
        return;
    }

    // Read available data from serial port
    while (serialPort.available() > 0) {
        char c = serialPort.read();

        if (c == ';') {
            // End of command - process it
            if (buffer.length() > 0) {
                processCommand(buffer);
                buffer = "";  // Clear buffer
                lastCommandTime = millis();  // Update timestamp
                timeoutCommandsSent = false;  // Reset timeout flag
            }
        } else if (c == '\n' || c == '\r') {
            // Ignore line feeds and carriage returns
            continue;
        } else {
            // Add character to buffer
            buffer += c;
        }
    }

    // Check for timeout (but skip if fetching JSON parameters)
    CanSDO::FetchResult fetchStatus = canSDO.GetJsonFetchStatus();

    if (fetchStatus == CanSDO::InProgress) {
        // Reset timeout while fetching parameters from VCU
        lastCommandTime = millis();
    } else if (millis() - lastCommandTime >= TIMEOUT_MS) {
        sendTimeoutCommands();
        // Reset timer to wait another 5 seconds before sending again
        lastCommandTime = millis();
    }
}

void SerialCommandHandler::SetEnabled(bool isEnabled) {
    enabled = isEnabled;
    Serial.print("*** Serial relay now ");
    Serial.print(enabled ? "ENABLED" : "DISABLED");
    Serial.println(" ***");

    if (!enabled) {
        // Clear buffer and reset state when disabled
        buffer = "";
        timeoutCommandsSent = true;  // Prevent timeout messages when disabled
    } else {
        // Reset timeout timer when enabled
        lastCommandTime = millis();
        timeoutCommandsSent = false;
    }
}

bool SerialCommandHandler::IsEnabled() {
    return enabled;
}

void SerialCommandHandler::processCommand(String command) {
    // Parse command in format "id:value"
    int colonIndex = command.indexOf(':');

    if (colonIndex == -1) {
        Serial.print("Invalid command format (missing ':'): ");
        Serial.println(command);
        return;
    }

    String idStr = command.substring(0, colonIndex);
    String valueStr = command.substring(colonIndex + 1);

    // Convert to integers/floats
    int id = idStr.toInt();
    double value = valueStr.toFloat();

    // Validate ID
    if (id == 0 && idStr != "0") {
        Serial.print("Invalid ID in command: ");
        Serial.println(command);
        return;
    }

    // Track this command
    lastCommandId = id;
    lastCommandIdTime = millis();

    // Send SDO request to ZombieVerter
    Serial.print("Received command - ID: ");
    Serial.print(id);
    Serial.print(", Value: ");
    Serial.println(value);

    CanSDO::SetResult result = canSDO.SetValue(id, value);

    switch (result) {
        case CanSDO::Ok:
            Serial.print("Successfully set parameter ");
            Serial.print(id);
            Serial.print(" to ");
            Serial.println(value);
            break;
        case CanSDO::ValueOutOfRange:
            Serial.print("Error: Value ");
            Serial.print(value);
            Serial.print(" out of range for parameter ");
            Serial.println(id);
            break;
        case CanSDO::UnknownIndex:
            Serial.print("Error: Unknown parameter ID ");
            Serial.println(id);
            break;
        case CanSDO::CommError:
            Serial.print("Error: Communication error setting parameter ");
            Serial.println(id);
            break;
    }
}

void SerialCommandHandler::sendTimeoutCommands() {
    lastTimeoutTime = millis();
    Serial.println("Timeout: No commands received for 5 seconds, sending default values");

    // IDs to set to 0 on timeout
    int timeoutIds[] = {155, 154, 144};

    for (int i = 0; i < 3; i++) {
        int id = timeoutIds[i];
        Serial.print("Setting parameter ");
        Serial.print(id);
        Serial.println(" to 0");

        CanSDO::SetResult result = canSDO.SetValue(id, 0.0);

        if (result != CanSDO::Ok) {
            Serial.print("Warning: Failed to set timeout parameter ");
            Serial.println(id);
        }
    }
}

unsigned long SerialCommandHandler::GetLastTimeoutTime() {
    return lastTimeoutTime;
}

int SerialCommandHandler::GetLastCommandId() {
    return lastCommandId;
}

unsigned long SerialCommandHandler::GetLastCommandIdTime() {
    return lastCommandIdTime;
}

#include "CanSDOQueue.h"
#include "CanSDO.h"

// SDO Command codes
#define SDO_READ           0x40
#define SDO_WRITE          0x23
#define SDO_RESPONSE_UPLOAD   0x43
#define SDO_RESPONSE_DOWNLOAD 0x60
#define SDO_ABORT          0x80

// ZOMBIE_NODE_ID and SDO_REP_ID_BASE are already defined in CanSDO.h

CanSDOQueue::CanSDOQueue()
    : state(IDLE)
    , stateStartTime(0)
    , nextRequestId(1)
    , currentRequest(nullptr)
    , completedRequests(0)
    , failedRequests(0)
    , timeoutRequests(0)
{
}

void CanSDOQueue::Setup() {
    Serial.println("CanSDOQueue initialized");
}

void CanSDOQueue::Loop() {
    processQueue();
}

void CanSDOQueue::ReadValue(int paramId, std::function<void(int32_t)> callback) {
    CanSDORequest* request = new CanSDORequest();
    request->type = CanSDORequest::READ;
    request->state = CanSDORequest::PENDING;
    request->index = SDO_INDEX_PARAM_UID | (paramId >> 8);
    request->subIndex = paramId & 0xFF;
    request->readCallback = callback;
    request->requestTime = millis();
    request->retryCount = 0;
    request->requestId = nextRequestId++;

    pendingRequests.push(request);

//    Serial.print("Queued READ request #");
//    Serial.print(request->requestId);
//    Serial.print(" for param ");
//    Serial.println(paramId);
}

void CanSDOQueue::WriteValue(int paramId, double value, std::function<void(int)> callback) {
    CanSDORequest* request = new CanSDORequest();
    request->type = CanSDORequest::WRITE;
    request->state = CanSDORequest::PENDING;
    request->index = SDO_INDEX_PARAM_UID | (paramId >> 8);
    request->subIndex = paramId & 0xFF;
    request->writeValue = (uint32_t)(value * 32);
    request->writeCallback = callback;
    request->requestTime = millis();
    request->retryCount = 0;
    request->requestId = nextRequestId++;

    pendingRequests.push(request);

//    Serial.print("Queued WRITE request #");
//    Serial.print(request->requestId);
//    Serial.print(" for param ");
//    Serial.print(paramId);
//    Serial.print(" = ");
//    Serial.println(value);
}

int32_t CanSDOQueue::ReadValueBlocking(int paramId, uint16_t timeoutMs) {
    // Create request without callback
    CanSDORequest* request = new CanSDORequest();
    request->type = CanSDORequest::READ;
    request->state = CanSDORequest::PENDING;
    request->index = SDO_INDEX_PARAM_UID | (paramId >> 8);
    request->subIndex = paramId & 0xFF;
    request->readCallback = nullptr;
    request->requestTime = millis();
    request->retryCount = 0;
    request->requestId = nextRequestId++;

    pendingRequests.push(request);

    // Wait for completion
    unsigned long start = millis();
    while (request->state == CanSDORequest::PENDING ||
           request->state == CanSDORequest::IN_PROGRESS) {
        if (millis() - start >= timeoutMs) {
            // Timeout - remove from queue if still pending
            return 0;
        }
        Loop();
        delay(1);
    }

    int32_t result = request->readValue;
    return result;
}

int CanSDOQueue::WriteValueBlocking(int paramId, double value, uint16_t timeoutMs) {
    // Create request without callback
    CanSDORequest* request = new CanSDORequest();
    request->type = CanSDORequest::WRITE;
    request->state = CanSDORequest::PENDING;
    request->index = SDO_INDEX_PARAM_UID | (paramId >> 8);
    request->subIndex = paramId & 0xFF;
    request->writeValue = (uint32_t)(value * 32);
    request->writeCallback = nullptr;
    request->requestTime = millis();
    request->retryCount = 0;
    request->requestId = nextRequestId++;

    pendingRequests.push(request);

    // Wait for completion
    unsigned long start = millis();
    while (request->state == CanSDORequest::PENDING ||
           request->state == CanSDORequest::IN_PROGRESS) {
        if (millis() - start >= timeoutMs) {
            // Timeout
            return 3;  // CommError
        }
        Loop();
        delay(1);
    }

    if (request->state == CanSDORequest::COMPLETED) {
        return 0;  // Ok
    } else if (request->state == CanSDORequest::FAILED) {
        return request->writeResult;
    } else {
        return 3;  // CommError
    }
}

void CanSDOQueue::processQueue() {
    unsigned long now = millis();

    switch (state) {
        case WAITING_FOR_RESPONSE: {
            // Check for response
            twai_message_t inMessage;
            bool foundResponse = false;

            while (twai_receive(&inMessage, pdMS_TO_TICKS(0)) == ESP_OK) {
                // Check if this is an SDO response
                if (inMessage.identifier == (SDO_REP_ID_BASE | ZOMBIE_NODE_ID)) {
                    // Check if it matches our current request
                    uint16_t responseIndex = inMessage.data[1] | (inMessage.data[2] << 8);
                    uint8_t responseSubIndex = inMessage.data[3];

                    if (currentRequest &&
                        responseIndex == currentRequest->index &&
                        responseSubIndex == currentRequest->subIndex) {
                        handleResponse(inMessage);
                        foundResponse = true;
                        break;
                    }
                }
            }

            if (foundResponse) {
                state = IDLE;
            } else if (now - stateStartTime >= REQUEST_TIMEOUT_MS) {
                handleTimeout();
            }
            break;
        }

        case RETRY_DELAY:
            if (now - stateStartTime >= RETRY_DELAY_MS) {
                handleRetry();
            }
            break;

        case IDLE:
            // Clean up completed request
            if (currentRequest) {
                delete currentRequest;
                currentRequest = nullptr;
            }

            // Process next pending request
            if (!pendingRequests.empty()) {
                currentRequest = pendingRequests.front();
                pendingRequests.pop();
                sendRequest(currentRequest);
                state = WAITING_FOR_RESPONSE;
                stateStartTime = now;
            }
            break;
    }
}

void CanSDOQueue::sendRequest(CanSDORequest* request) {
    if (!request) return;

//    Serial.print("Sending request #");
//    Serial.print(request->requestId);
//    Serial.print(" (");
//    Serial.print(request->type == CanSDORequest::READ ? "READ" : "WRITE");
//    Serial.print(") idx=0x");
//    Serial.print(request->index, HEX);
//    Serial.print(" sub=");
//    Serial.println(request->subIndex);

    if (request->type == CanSDORequest::READ) {
        sendReadSDO(request->index, request->subIndex);
    } else {
        sendWriteSDO(request->index, request->subIndex, request->writeValue);
    }

    request->state = CanSDORequest::IN_PROGRESS;
}

void CanSDOQueue::sendReadSDO(uint16_t index, uint8_t subIndex) {
    twai_message_t outMessage;
    outMessage.extd = false;
    outMessage.identifier = 0x600 | ZOMBIE_NODE_ID;
    outMessage.data_length_code = 8;
    outMessage.data[0] = SDO_READ;
    outMessage.data[1] = index & 0xFF;
    outMessage.data[2] = (index >> 8) & 0xFF;
    outMessage.data[3] = subIndex;
    outMessage.data[4] = 0;
    outMessage.data[5] = 0;
    outMessage.data[6] = 0;
    outMessage.data[7] = 0;

    twai_transmit(&outMessage, pdMS_TO_TICKS(10));
}

void CanSDOQueue::sendWriteSDO(uint16_t index, uint8_t subIndex, uint32_t value) {
    twai_message_t outMessage;
    outMessage.extd = false;
    outMessage.identifier = 0x600 | ZOMBIE_NODE_ID;
    outMessage.data_length_code = 8;
    outMessage.data[0] = SDO_WRITE;
    outMessage.data[1] = index & 0xFF;
    outMessage.data[2] = (index >> 8) & 0xFF;
    outMessage.data[3] = subIndex;
    *(uint32_t*)&outMessage.data[4] = value;

    twai_transmit(&outMessage, pdMS_TO_TICKS(10));
}

void CanSDOQueue::handleResponse(const twai_message_t& message) {
    if (!currentRequest) return;

    uint8_t command = message.data[0];

    if (command == SDO_RESPONSE_UPLOAD && currentRequest->type == CanSDORequest::READ) {
        // Read response
        currentRequest->readValue = *(int32_t*)&message.data[4];
        currentRequest->state = CanSDORequest::COMPLETED;
        currentRequest->responseTime = millis();

//        Serial.print("Request #");
//        Serial.print(currentRequest->requestId);
//        Serial.print(" completed: value = ");
//        Serial.println(currentRequest->readValue);

        if (currentRequest->readCallback) {
            currentRequest->readCallback(currentRequest->readValue);
        }

        completedRequests++;

    } else if (command == SDO_RESPONSE_DOWNLOAD && currentRequest->type == CanSDORequest::WRITE) {
        // Write response - success
        currentRequest->writeResult = 0;  // Ok
        currentRequest->state = CanSDORequest::COMPLETED;
        currentRequest->responseTime = millis();
//
//        Serial.print("Request #");
//        Serial.print(currentRequest->requestId);
//        Serial.println(" write completed successfully");

        if (currentRequest->writeCallback) {
            currentRequest->writeCallback(0);  // Ok
        }

        completedRequests++;

    } else if (command == SDO_ABORT) {
        // Abort response
        uint32_t abortCode = *(uint32_t*)&message.data[4];
        currentRequest->state = CanSDORequest::FAILED;

//        Serial.print("Request #");
//        Serial.print(currentRequest->requestId);
//        Serial.print(" aborted with code 0x");
//        Serial.println(abortCode, HEX);

        int result = (abortCode == 0x06090030) ? 1 : 2;  // ValueOutOfRange : UnknownIndex

        if (currentRequest->type == CanSDORequest::WRITE && currentRequest->writeCallback) {
            currentRequest->writeCallback(result);
        }

        failedRequests++;
    }
}

void CanSDOQueue::handleTimeout() {
    if (!currentRequest) return;

    if (currentRequest->retryCount < MAX_RETRIES) {
//        Serial.print("Request #");
//        Serial.print(currentRequest->requestId);
//        Serial.print(" timeout, retry ");
//        Serial.print(currentRequest->retryCount + 1);
//        Serial.print("/");
//        Serial.println(MAX_RETRIES);

        state = RETRY_DELAY;
        stateStartTime = millis();
    } else {
//        Serial.print("Request #");
//        Serial.print(currentRequest->requestId);
//        Serial.println(" failed after max retries");

        currentRequest->state = CanSDORequest::TIMEOUT;

        if (currentRequest->type == CanSDORequest::READ && currentRequest->readCallback) {
            currentRequest->readCallback(0);
        } else if (currentRequest->type == CanSDORequest::WRITE && currentRequest->writeCallback) {
            currentRequest->writeCallback(3);  // CommError
        }

        timeoutRequests++;
        state = IDLE;
    }
}

void CanSDOQueue::handleRetry() {
    if (!currentRequest) return;

    currentRequest->retryCount++;
    sendRequest(currentRequest);
    state = WAITING_FOR_RESPONSE;
    stateStartTime = millis();
}

size_t CanSDOQueue::GetQueueDepth() {
    return pendingRequests.size() + (currentRequest ? 1 : 0);
}

bool CanSDOQueue::IsBusy() {
    return state != IDLE || !pendingRequests.empty();
}

void CanSDOQueue::ClearQueue() {
    // Clear pending requests
    while (!pendingRequests.empty()) {
        CanSDORequest* req = pendingRequests.front();
        pendingRequests.pop();
        delete req;
    }

    // Cancel current request
    if (currentRequest) {
        delete currentRequest;
        currentRequest = nullptr;
    }

    state = IDLE;
//    Serial.println("CanSDOQueue cleared");
}

uint32_t CanSDOQueue::GetCompletedRequests() {
    return completedRequests;
}

uint32_t CanSDOQueue::GetFailedRequests() {
    return failedRequests;
}

uint32_t CanSDOQueue::GetTimeoutRequests() {
    return timeoutRequests;
}

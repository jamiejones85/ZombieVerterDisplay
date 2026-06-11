#ifndef CANSDOQUEUE_H
#define CANSDOQUEUE_H

#include <Arduino.h>
#include <functional>
#include <queue>
#include "driver/twai.h"

// Don't include CanSDO.h to avoid circular dependency
// Constants are defined in CanSDO.h and available when this is used

// SDO Request structure
struct CanSDORequest {
    enum Type { READ, WRITE };
    enum State { PENDING, IN_PROGRESS, COMPLETED, FAILED, TIMEOUT };

    Type type;
    State state;
    uint16_t index;        // SDO index
    uint8_t subIndex;      // SDO subindex
    uint32_t writeValue;   // Value to write (for WRITE requests)

    // Response data
    int32_t readValue;     // Value read (for READ requests)
    int writeResult;       // Result of write operation (CanSDO::SetResult)

    // Callbacks
    std::function<void(int32_t)> readCallback;
    std::function<void(int)> writeCallback;

    // Timing
    unsigned long requestTime;
    unsigned long responseTime;
    uint8_t retryCount;

    // Request ID for debugging
    uint32_t requestId;
};

class CanSDOQueue {
public:
    CanSDOQueue();
    void Setup();
    void Loop();  // Process queue, handle responses

    // Non-blocking request methods with callbacks
    void ReadValue(int paramId, std::function<void(int32_t)> callback);
    void WriteValue(int paramId, double value, std::function<void(int)> callback);

    // Blocking methods for backward compatibility
    int32_t ReadValueBlocking(int paramId, uint16_t timeoutMs = 500);
    int WriteValueBlocking(int paramId, double value, uint16_t timeoutMs = 500);

    // Queue management
    size_t GetQueueDepth();
    bool IsBusy();
    void ClearQueue();

    // Statistics
    uint32_t GetCompletedRequests();
    uint32_t GetFailedRequests();
    uint32_t GetTimeoutRequests();

private:
    // Configuration
    static const uint8_t MAX_RETRIES = 3;
    static const uint16_t REQUEST_TIMEOUT_MS = 100;
    static const uint16_t RETRY_DELAY_MS = 50;

    // State
    enum QueueState { IDLE, WAITING_FOR_RESPONSE, RETRY_DELAY };
    QueueState state;
    unsigned long stateStartTime;
    uint32_t nextRequestId;

    // Queue
    std::queue<CanSDORequest*> pendingRequests;
    CanSDORequest* currentRequest;

    // Statistics
    uint32_t completedRequests;
    uint32_t failedRequests;
    uint32_t timeoutRequests;

    // Helper methods
    void processQueue();
    void sendRequest(CanSDORequest* request);
    void handleResponse(const twai_message_t& message);
    void handleTimeout();
    void handleRetry();
    void completeRequest(CanSDORequest* request);

    // SDO protocol helpers
    void sendReadSDO(uint16_t index, uint8_t subIndex);
    void sendWriteSDO(uint16_t index, uint8_t subIndex, uint32_t value);
};

#endif // CANSDOQUEUE_H

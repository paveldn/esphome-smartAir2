#ifndef _HAIER_CLIMATE_H
#define _HAIER_CLIMATE_H

#include <chrono>
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

#if ESP8266
// No mutexes for ESP8266 just make dummy classes and pray...
struct Mutex
{
    void lock() {};
    void unlock() {};
};

struct Lock
{
    Lock(const Mutex&) {};
}; 
#else
#include <mutex>
typedef std::mutex Mutex;
typedef std::lock_guard<Mutex> Lock;
#endif

namespace esphome {
namespace haier {

class HaierClimate :    public esphome::Component,
                        public esphome::climate::Climate,
                        public esphome::uart::UARTDevice
{
public:
    HaierClimate() = delete;
    HaierClimate(const HaierClimate&) = delete;
    HaierClimate& operator=(const HaierClimate&) = delete;
    HaierClimate(esphome::uart::UARTComponent* parent);
    ~HaierClimate();
    void setup() override;
    void loop() override;
    void control(const esphome::climate::ClimateCall &call) override;
    float get_setup_priority() const override { return esphome::setup_priority::HARDWARE ; }
    void set_beeper_echo(bool beeper);
    bool get_beeper_echo() const;
    void set_display_state(bool state);
    bool get_display_state() const;
protected:
    esphome::climate::ClimateTraits traits() override;
    void sendData(const uint8_t * message, size_t size, bool withCrc = true);
    void processStatus(const uint8_t* packet, uint8_t size);
    void handleIncomingPacket();
    void getSerialData();
    void sendControlPacket(const esphome::climate::ClimateCall* control = NULL);
private:
    enum ProtocolPhases
    {
        // Initialization
        psSendingFirstStatusRequest = 0,
        psWaitingFirstStatusAnswer,
        // Functional state
        psIdle,
        psSendingStatusRequest,
        psWaitingStatusAnswer,
    };
    ProtocolPhases      mPhase;
    Mutex               mReadMutex;
    uint8_t*            mLastPacket;
    uint8_t             mFanModeFanSpeed;
    uint8_t             mOtherModesFanSpeed;
    bool                mBeeperEcho;    
    bool                mDisplayStatus;
    bool                mForceSendControl;
    esphome::climate::ClimateTraits         mTraits;
    std::chrono::steady_clock::time_point   mLastByteTimestamp;         // For packet timeout
    std::chrono::steady_clock::time_point   mLastRequestTimestamp;      // For answer timeout
    std::chrono::steady_clock::time_point   mLastValidStatusTimestamp;  // For protocol timeout
    std::chrono::steady_clock::time_point   mLastStatusRequest; // To request AC status
    std::chrono::steady_clock::time_point   mLastSignalRequest; // To send WiFI signal level

};

} // namespace haier
} // namespace esphome


#endif // _HAIER_CLIMATE_H
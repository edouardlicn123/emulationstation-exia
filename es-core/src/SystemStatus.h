//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  SystemStatus.h
//
//  Queries system status information from the operating system.
//  This includes Bluetooth, Wi-Fi, the wired network, cellular and battery.
//

#ifndef ES_CORE_SYSTEM_STATUS_H
#define ES_CORE_SYSTEM_STATUS_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class SystemStatus
{
public:
    ~SystemStatus();
    static SystemStatus& getInstance();

    void setCheckFlags();
    void setPolling(const bool state);
    void setPollImmediately(const bool state) { mPollImmediately = state; }
    const bool getPollImmediately() { return mPollImmediately; }

    struct Status {
        bool hasBluetooth;
        bool hasWifi;
        bool hasEthernet;
        bool hasCellular;
        bool hasBattery;
        bool batteryCharging;
        int batteryCapacity;
        // Exia addition, the network name of the connected Wi-Fi network and the address of the
        // connected wired interface. Unlike the flags above these are not read atomically, they
        // are copied while holding the status mutex.
        std::string wifiNetworkName;
        std::string ethernetAddress;
        Status()
            : hasBluetooth {false}
            , hasWifi {false}
            , hasEthernet {false}
            , hasCellular {false}
            , hasBattery {false}
            , batteryCharging {false}
            , batteryCapacity {0}
        {
        }
    };

    Status getStatus(const bool update = true);

    static constexpr int updateTime {300};
    static constexpr int pollingTime {2500};

private:
    SystemStatus() noexcept;

    void pollStatus();

    void getStatusBluetooth();
    void getStatusWifi();
    void getStatusEthernet();
    void getStatusCellular();
    void getStatusBattery();

    bool mCheckBluetooth;
    bool mCheckWifi;
    bool mCheckCellular;
    bool mCheckBattery;

    std::unique_ptr<std::thread> mPollThread;
    Status mStatus;
    std::mutex mStatusMutex;

    std::atomic<bool> mExitPolling;
    std::atomic<bool> mPollImmediately;

    std::atomic<bool> mHasBluetooth;
    std::atomic<bool> mHasWifi;
    std::atomic<bool> mHasCellular;
    std::atomic<bool> mHasBattery;
    std::atomic<bool> mBatteryCharging;
    std::atomic<int> mBatteryCapacity;

    // Exia additions, the network name of the connected Wi-Fi network and the address of the
    // connected wired interface. Written by the polling thread while it holds the status mutex
    // and copied in getStatus() while holding the same mutex.
    std::atomic<bool> mHasEthernet;
    std::string mWifiNetworkName;
    std::string mEthernetAddress;
};

#endif // ES_CORE_SYSTEM_STATUS_H

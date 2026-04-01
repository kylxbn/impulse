#pragma once

#include "mpris/MprisModel.hpp"

#include <systemd/sd-bus.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MprisService {
public:
    MprisService();
    ~MprisService();

    MprisService(const MprisService&) = delete;
    MprisService& operator=(const MprisService&) = delete;

    [[nodiscard]] bool active() const;
    [[nodiscard]] std::string serviceName() const;

    void shutdown();
    void publishSnapshot(MprisSnapshot snapshot);
    void notifySeeked(int64_t position_us);
    [[nodiscard]] std::vector<MprisCommand> takePendingCommands();

    static int onRaise(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onQuit(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onNext(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onPrevious(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onPause(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onPlayPause(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onStop(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onPlay(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onSeek(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onSetPosition(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onOpenUri(sd_bus_message* message, void* userdata, sd_bus_error* error);

    static int getRootProperty(sd_bus* bus,
                               const char* path,
                               const char* interface,
                               const char* property,
                               sd_bus_message* reply,
                               void* userdata,
                               sd_bus_error* error);
    static int getPlayerProperty(sd_bus* bus,
                                 const char* path,
                                 const char* interface,
                                 const char* property,
                                 sd_bus_message* reply,
                                 void* userdata,
                                 sd_bus_error* error);
    static int setPlayerRate(sd_bus* bus,
                             const char* path,
                             const char* interface,
                             const char* property,
                             sd_bus_message* value,
                             void* userdata,
                             sd_bus_error* error);
    static int setPlayerLoopStatus(sd_bus* bus,
                                   const char* path,
                                   const char* interface,
                                   const char* property,
                                   sd_bus_message* value,
                                   void* userdata,
                                   sd_bus_error* error);
    static int setPlayerShuffle(sd_bus* bus,
                                const char* path,
                                const char* interface,
                                const char* property,
                                sd_bus_message* value,
                                void* userdata,
                                sd_bus_error* error);
    static int setPlayerVolume(sd_bus* bus,
                               const char* path,
                               const char* interface,
                               const char* property,
                               sd_bus_message* value,
                               void* userdata,
                               sd_bus_error* error);

private:
    bool initialize();
    void signalBusThread() noexcept;
    void runLoop(std::stop_token stop_token);
    void emitChangedProperties(const MprisSnapshot& previous,
                               const MprisSnapshot& current,
                               bool force_emit);
    void drainSeekedSignals();
    void queueCommand(MprisCommand command);

    [[nodiscard]] MprisSnapshot snapshot() const;

    static int appendMetadata(sd_bus_message* reply, const MprisTrackMetadata& metadata);

    mutable std::mutex      mutex_;
    MprisSnapshot           snapshot_{};
    std::vector<MprisCommand> pending_commands_;
    std::vector<int64_t>    pending_seeked_positions_;
    std::string             service_name_;

    std::atomic<bool> active_{false};
    std::atomic<bool> stopping_{false};
    std::jthread      bus_thread_;
    int               wake_fd_ = -1;
    sd_bus*           bus_ = nullptr;
    sd_bus_slot*      root_slot_ = nullptr;
    sd_bus_slot*      player_slot_ = nullptr;
};

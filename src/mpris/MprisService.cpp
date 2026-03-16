#include "MprisService.hpp"

#include <systemd/sd-bus.h>

#include <poll.h>
#include <sys/eventfd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <string_view>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

const sd_bus_vtable kRootVTable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Raise", "", "", &MprisService::onRaise, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Quit", "", "", &MprisService::onQuit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("CanQuit", "b", &MprisService::getRootProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanRaise", "b", &MprisService::getRootProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("HasTrackList", "b", &MprisService::getRootProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Identity", "s", &MprisService::getRootProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("DesktopEntry", "s", &MprisService::getRootProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedUriSchemes", "as", &MprisService::getRootProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedMimeTypes", "as", &MprisService::getRootProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END
};

const sd_bus_vtable kPlayerVTable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Next", "", "", &MprisService::onNext, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Previous", "", "", &MprisService::onPrevious, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Pause", "", "", &MprisService::onPause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("PlayPause", "", "", &MprisService::onPlayPause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "", &MprisService::onStop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Play", "", "", &MprisService::onPlay, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Seek", "x", "", &MprisService::onSeek, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPosition", "ox", "", &MprisService::onSetPosition, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("OpenUri", "s", "", &MprisService::onOpenUri, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Seeked", "x", 0),
    SD_BUS_PROPERTY("PlaybackStatus", "s", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("LoopStatus", "s", &MprisService::getPlayerProperty, &MprisService::setPlayerLoopStatus, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("Rate", "d", &MprisService::getPlayerProperty, &MprisService::setPlayerRate, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("Shuffle", "b", &MprisService::getPlayerProperty, &MprisService::setPlayerShuffle, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Metadata", "a{sv}", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("Volume", "d", &MprisService::getPlayerProperty, &MprisService::setPlayerVolume, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Position", "x", &MprisService::getPlayerProperty, 0, 0),
    SD_BUS_PROPERTY("MinimumRate", "d", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MaximumRate", "d", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoNext", "b", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanGoPrevious", "b", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanPlay", "b", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanPause", "b", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanSeek", "b", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanControl", "b", &MprisService::getPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END
};

int appendStringArray(sd_bus_message* message, const std::vector<std::string>& values) {
    int ret = sd_bus_message_open_container(message, 'a', "s");
    if (ret < 0)
        return ret;

    for (const auto& value : values) {
        ret = sd_bus_message_append(message, "s", value.c_str());
        if (ret < 0)
            return ret;
    }

    return sd_bus_message_close_container(message);
}

int appendMetadataEntry(sd_bus_message* reply,
                        const char* key,
                        const char* variant_signature,
                        const auto& append_value) {
    int ret = sd_bus_message_open_container(reply, 'e', "sv");
    if (ret < 0)
        return ret;

    ret = sd_bus_message_append(reply, "s", key);
    if (ret < 0)
        return ret;

    ret = sd_bus_message_open_container(reply, 'v', variant_signature);
    if (ret < 0)
        return ret;

    ret = append_value();
    if (ret < 0)
        return ret;

    ret = sd_bus_message_close_container(reply);
    if (ret < 0)
        return ret;

    return sd_bus_message_close_container(reply);
}

std::vector<char*> makeMutableStrv(const std::vector<const char*>& names) {
    std::vector<char*> result;
    result.reserve(names.size() + 1);
    for (const char* name : names)
        result.push_back(const_cast<char*>(name));
    result.push_back(nullptr);
    return result;
}

MprisCommand makeCommand(MprisCommandType type) {
    MprisCommand command{};
    command.type = type;
    return command;
}

bool affectsEmittedPlayerProperties(const MprisSnapshot& lhs, const MprisSnapshot& rhs) {
    return lhs.playback_status != rhs.playback_status ||
           lhs.loop_status != rhs.loop_status ||
           lhs.shuffle != rhs.shuffle ||
           lhs.can_go_next != rhs.can_go_next ||
           lhs.can_go_previous != rhs.can_go_previous ||
           lhs.can_play != rhs.can_play ||
           lhs.can_pause != rhs.can_pause ||
           lhs.can_seek != rhs.can_seek ||
           lhs.can_control != rhs.can_control ||
           lhs.rate != rhs.rate ||
           lhs.minimum_rate != rhs.minimum_rate ||
           lhs.maximum_rate != rhs.maximum_rate ||
           lhs.volume != rhs.volume ||
           !(lhs.track == rhs.track);
}

int busTimeoutToPollTimeoutMs(uint64_t bus_timeout_usec) {
    if (bus_timeout_usec == UINT64_MAX)
        return -1;

    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;

    const uint64_t now_usec =
        static_cast<uint64_t>(now.tv_sec) * 1'000'000u +
        static_cast<uint64_t>(now.tv_nsec / 1'000u);
    if (bus_timeout_usec <= now_usec)
        return 0;

    const uint64_t delta_usec = bus_timeout_usec - now_usec;
    const uint64_t timeout_ms = (delta_usec + 999u) / 1'000u;
    return static_cast<int>(std::min<uint64_t>(timeout_ms,
                                               static_cast<uint64_t>(std::numeric_limits<int>::max())));
}

void drainWakeFd(int fd) {
    if (fd < 0)
        return;

    uint64_t value = 0;
    while (read(fd, &value, sizeof(value)) == sizeof(value)) {}
}

}  // namespace

MprisService::MprisService() {
    if (!initialize())
        shutdown();
}

MprisService::~MprisService() {
    shutdown();
}

bool MprisService::active() const {
    return active_.load(std::memory_order_relaxed);
}

std::string MprisService::serviceName() const {
    std::scoped_lock lock(mutex_);
    return service_name_;
}

void MprisService::shutdown() {
    if (stopping_.exchange(true, std::memory_order_acq_rel))
        return;

    if (bus_thread_.joinable()) {
        bus_thread_.request_stop();
        signalBusThread();
        bus_thread_.join();
    }

    if (root_slot_) {
        sd_bus_slot_unref(root_slot_);
        root_slot_ = nullptr;
    }
    if (player_slot_) {
        sd_bus_slot_unref(player_slot_);
        player_slot_ = nullptr;
    }
    if (bus_) {
        sd_bus_flush_close_unref(bus_);
        bus_ = nullptr;
    }
    if (wake_fd_ >= 0) {
        close(wake_fd_);
        wake_fd_ = -1;
    }

    active_.store(false, std::memory_order_relaxed);
}

void MprisService::publishSnapshot(MprisSnapshot snapshot) {
    bool should_signal = false;
    std::scoped_lock lock(mutex_);
    should_signal = affectsEmittedPlayerProperties(snapshot_, snapshot);
    snapshot_ = std::move(snapshot);
    if (should_signal)
        signalBusThread();
}

void MprisService::notifySeeked(int64_t position_us) {
    std::scoped_lock lock(mutex_);
    pending_seeked_positions_.push_back(std::max<int64_t>(0, position_us));
    signalBusThread();
}

std::vector<MprisCommand> MprisService::takePendingCommands() {
    std::scoped_lock lock(mutex_);
    std::vector<MprisCommand> commands;
    commands.swap(pending_commands_);
    return commands;
}

bool MprisService::initialize() {
    if (sd_bus_open_user(&bus_) < 0)
        return false;

    sd_bus_set_exit_on_disconnect(bus_, 0);

    wake_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wake_fd_ < 0)
        return false;

    if (sd_bus_add_object_vtable(bus_,
                                 &root_slot_,
                                 kMprisObjectPath.data(),
                                 kMprisRootInterface.data(),
                                 kRootVTable,
                                 this) < 0) {
        return false;
    }

    if (sd_bus_add_object_vtable(bus_,
                                 &player_slot_,
                                 kMprisObjectPath.data(),
                                 kMprisPlayerInterface.data(),
                                 kPlayerVTable,
                                 this) < 0) {
        return false;
    }

    int ret = sd_bus_request_name(bus_, kMprisBaseServiceName.data(), 0);
    if (ret < 0) {
        const std::string fallback = std::format("{}.instance{}", kMprisBaseServiceName, static_cast<long>(::getpid()));
        ret = sd_bus_request_name(bus_, fallback.c_str(), 0);
        if (ret < 0)
            return false;

        std::scoped_lock lock(mutex_);
        service_name_ = fallback;
    } else {
        std::scoped_lock lock(mutex_);
        service_name_ = std::string(kMprisBaseServiceName);
    }

    active_.store(true, std::memory_order_relaxed);
    bus_thread_ = std::jthread([this](std::stop_token stop_token) {
        runLoop(stop_token);
    });
    return true;
}

void MprisService::signalBusThread() noexcept {
    if (wake_fd_ < 0)
        return;

    const uint64_t wake_value = 1;
    const ssize_t written = write(wake_fd_, &wake_value, sizeof(wake_value));
    (void) written;
}

void MprisService::runLoop(std::stop_token stop_token) {
    MprisSnapshot last_snapshot{};
    bool          has_last_snapshot = false;

    while (!stop_token.stop_requested()) {
        while (bus_) {
            int ret = sd_bus_process(bus_, nullptr);
            if (ret < 0) {
                active_.store(false, std::memory_order_relaxed);
                return;
            }
            if (ret == 0)
                break;
        }

        const MprisSnapshot current = snapshot();
        emitChangedProperties(last_snapshot, current, !has_last_snapshot);
        last_snapshot = current;
        has_last_snapshot = true;
        drainSeekedSignals();

        if (!bus_) {
            active_.store(false, std::memory_order_relaxed);
            return;
        }

        const int bus_fd = sd_bus_get_fd(bus_);
        if (bus_fd < 0) {
            active_.store(false, std::memory_order_relaxed);
            return;
        }

        const int bus_events = sd_bus_get_events(bus_);
        if (bus_events < 0) {
            active_.store(false, std::memory_order_relaxed);
            return;
        }

        uint64_t bus_timeout_usec = UINT64_MAX;
        int ret = sd_bus_get_timeout(bus_, &bus_timeout_usec);
        if (ret < 0) {
            active_.store(false, std::memory_order_relaxed);
            return;
        }

        std::array<pollfd, 2> poll_fds{};
        nfds_t poll_count = 0;
        poll_fds[poll_count++] = pollfd{
            .fd = bus_fd,
            .events = static_cast<short>(bus_events),
            .revents = 0,
        };
        if (wake_fd_ >= 0) {
            poll_fds[poll_count++] = pollfd{
                .fd = wake_fd_,
                .events = POLLIN,
                .revents = 0,
            };
        }

        ret = poll(poll_fds.data(),
                   poll_count,
                   busTimeoutToPollTimeoutMs(bus_timeout_usec));
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            active_.store(false, std::memory_order_relaxed);
            return;
        }

        if (wake_fd_ >= 0 && poll_fds[poll_count - 1].fd == wake_fd_ &&
            (poll_fds[poll_count - 1].revents & POLLIN)) {
            drainWakeFd(wake_fd_);
        }
    }

    active_.store(false, std::memory_order_relaxed);
}

void MprisService::emitChangedProperties(const MprisSnapshot& previous,
                                         const MprisSnapshot& current,
                                         bool force_emit) {
    if (!bus_)
        return;

    std::vector<const char*> player_changes;
    if (force_emit || previous.playback_status != current.playback_status)
        player_changes.push_back("PlaybackStatus");
    if (force_emit || previous.loop_status != current.loop_status)
        player_changes.push_back("LoopStatus");
    if (force_emit || previous.rate != current.rate)
        player_changes.push_back("Rate");
    if (force_emit || previous.shuffle != current.shuffle)
        player_changes.push_back("Shuffle");
    if (force_emit || !(previous.track == current.track))
        player_changes.push_back("Metadata");
    if (force_emit || previous.volume != current.volume)
        player_changes.push_back("Volume");
    if (force_emit || previous.minimum_rate != current.minimum_rate)
        player_changes.push_back("MinimumRate");
    if (force_emit || previous.maximum_rate != current.maximum_rate)
        player_changes.push_back("MaximumRate");
    if (force_emit || previous.can_go_next != current.can_go_next)
        player_changes.push_back("CanGoNext");
    if (force_emit || previous.can_go_previous != current.can_go_previous)
        player_changes.push_back("CanGoPrevious");
    if (force_emit || previous.can_play != current.can_play)
        player_changes.push_back("CanPlay");
    if (force_emit || previous.can_pause != current.can_pause)
        player_changes.push_back("CanPause");
    if (force_emit || previous.can_seek != current.can_seek)
        player_changes.push_back("CanSeek");
    if (force_emit || previous.can_control != current.can_control)
        player_changes.push_back("CanControl");

    if (!player_changes.empty()) {
        auto names = makeMutableStrv(player_changes);
        sd_bus_emit_properties_changed_strv(bus_,
                                            kMprisObjectPath.data(),
                                            kMprisPlayerInterface.data(),
                                            names.data());
    }
}

void MprisService::drainSeekedSignals() {
    std::vector<int64_t> positions;
    {
        std::scoped_lock lock(mutex_);
        positions.swap(pending_seeked_positions_);
    }

    for (const int64_t position : positions)
        sd_bus_emit_signal(bus_,
                           kMprisObjectPath.data(),
                           kMprisPlayerInterface.data(),
                           "Seeked",
                           "x",
                           position);
}

void MprisService::queueCommand(MprisCommand command) {
    std::scoped_lock lock(mutex_);
    pending_commands_.push_back(std::move(command));
}

MprisSnapshot MprisService::snapshot() const {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

int MprisService::appendMetadata(sd_bus_message* reply, const MprisTrackMetadata& metadata) {
    int ret = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (ret < 0)
        return ret;

    if (!metadata.track_id.empty()) {
        ret = appendMetadataEntry(reply, "mpris:trackid", "o", [&] {
            return sd_bus_message_append(reply, "o", metadata.track_id.c_str());
        });
        if (ret < 0)
            return ret;
    }

    if (metadata.length_us > 0) {
        ret = appendMetadataEntry(reply, "mpris:length", "x", [&] {
            return sd_bus_message_append(reply, "x", metadata.length_us);
        });
        if (ret < 0)
            return ret;
    }

    if (!metadata.url.empty()) {
        ret = appendMetadataEntry(reply, "xesam:url", "s", [&] {
            return sd_bus_message_append(reply, "s", metadata.url.c_str());
        });
        if (ret < 0)
            return ret;
    }

    if (!metadata.art_url.empty()) {
        ret = appendMetadataEntry(reply, "mpris:artUrl", "s", [&] {
            return sd_bus_message_append(reply, "s", metadata.art_url.c_str());
        });
        if (ret < 0)
            return ret;
    }

    if (!metadata.title.empty()) {
        ret = appendMetadataEntry(reply, "xesam:title", "s", [&] {
            return sd_bus_message_append(reply, "s", metadata.title.c_str());
        });
        if (ret < 0)
            return ret;
    }

    if (!metadata.artists.empty()) {
        ret = appendMetadataEntry(reply, "xesam:artist", "as", [&] {
            return appendStringArray(reply, metadata.artists);
        });
        if (ret < 0)
            return ret;
    }

    if (!metadata.album.empty()) {
        ret = appendMetadataEntry(reply, "xesam:album", "s", [&] {
            return sd_bus_message_append(reply, "s", metadata.album.c_str());
        });
        if (ret < 0)
            return ret;
    }

    if (!metadata.genres.empty()) {
        ret = appendMetadataEntry(reply, "xesam:genre", "as", [&] {
            return appendStringArray(reply, metadata.genres);
        });
        if (ret < 0)
            return ret;
    }

    if (metadata.has_track_number) {
        ret = appendMetadataEntry(reply, "xesam:trackNumber", "i", [&] {
            return sd_bus_message_append(reply, "i", metadata.track_number);
        });
        if (ret < 0)
            return ret;
    }

    return sd_bus_message_close_container(reply);
}

int MprisService::onRaise(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    static_cast<MprisService*>(userdata)->queueCommand(makeCommand(MprisCommandType::Raise));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onQuit(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    static_cast<MprisService*>(userdata)->queueCommand(makeCommand(MprisCommandType::Quit));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onNext(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    static_cast<MprisService*>(userdata)->queueCommand(makeCommand(MprisCommandType::Next));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onPrevious(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    static_cast<MprisService*>(userdata)->queueCommand(makeCommand(MprisCommandType::Previous));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onPause(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    static_cast<MprisService*>(userdata)->queueCommand(makeCommand(MprisCommandType::Pause));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onPlayPause(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    static_cast<MprisService*>(userdata)->queueCommand(makeCommand(MprisCommandType::PlayPause));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onStop(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    static_cast<MprisService*>(userdata)->queueCommand(makeCommand(MprisCommandType::Stop));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onPlay(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    static_cast<MprisService*>(userdata)->queueCommand(makeCommand(MprisCommandType::Play));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onSeek(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    int64_t offset_us = 0;
    int ret = sd_bus_message_read(message, "x", &offset_us);
    if (ret < 0)
        return ret;

    auto command = makeCommand(MprisCommandType::SeekRelative);
    command.position_us = offset_us;
    static_cast<MprisService*>(userdata)->queueCommand(std::move(command));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onSetPosition(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    const char* track_id = nullptr;
    int64_t     position_us = 0;
    int ret = sd_bus_message_read(message, "ox", &track_id, &position_us);
    if (ret < 0)
        return ret;

    auto* self = static_cast<MprisService*>(userdata);
    const auto snapshot = self->snapshot();
    if (snapshot.track.track_id != std::string_view(track_id)) {
        return sd_bus_reply_method_return(message, nullptr);
    }

    auto command = makeCommand(MprisCommandType::SetPosition);
    command.position_us = position_us;
    command.track_id = track_id;
    self->queueCommand(std::move(command));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::onOpenUri(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* uri = nullptr;
    int ret = sd_bus_message_read(message, "s", &uri);
    if (ret < 0)
        return ret;

    const auto path = fileUriToPath(uri);
    if (!path || !isSupportedOpenUri(*path)) {
        return sd_bus_error_setf(error,
                                 SD_BUS_ERROR_NOT_SUPPORTED,
                                 "Unsupported URI: %s",
                                 uri);
    }

    auto command = makeCommand(MprisCommandType::OpenUri);
    command.uri = uri;
    static_cast<MprisService*>(userdata)->queueCommand(std::move(command));
    return sd_bus_reply_method_return(message, nullptr);
}

int MprisService::getRootProperty(sd_bus* /*bus*/,
                                  const char* /*path*/,
                                  const char* /*interface*/,
                                  const char* property,
                                  sd_bus_message* reply,
                                  void* /*userdata*/,
                                  sd_bus_error* /*error*/) {
    const std::string_view name(property);
    if (name == "CanQuit")
        return sd_bus_message_append(reply, "b", 1);
    if (name == "CanRaise")
        return sd_bus_message_append(reply, "b", 1);
    if (name == "HasTrackList")
        return sd_bus_message_append(reply, "b", 0);
    if (name == "Identity")
        return sd_bus_message_append(reply, "s", kMprisIdentity.data());
    if (name == "DesktopEntry")
        return sd_bus_message_append(reply, "s", kMprisDesktopEntry.data());
    if (name == "SupportedUriSchemes")
        return appendStringArray(reply, supportedUriSchemes());
    if (name == "SupportedMimeTypes")
        return appendStringArray(reply, supportedMimeTypes());
    return -EINVAL;
}

int MprisService::getPlayerProperty(sd_bus* /*bus*/,
                                    const char* /*path*/,
                                    const char* /*interface*/,
                                    const char* property,
                                    sd_bus_message* reply,
                                    void* userdata,
                                    sd_bus_error* /*error*/) {
    const auto snapshot = static_cast<MprisService*>(userdata)->snapshot();
    const std::string_view name(property);

    if (name == "PlaybackStatus")
        return sd_bus_message_append(reply, "s", playbackStatusToMprisString(snapshot.playback_status));
    if (name == "LoopStatus")
        return sd_bus_message_append(reply, "s", mprisLoopStatusToString(snapshot.loop_status));
    if (name == "Rate")
        return sd_bus_message_append(reply, "d", snapshot.rate);
    if (name == "Shuffle")
        return sd_bus_message_append(reply, "b", snapshot.shuffle ? 1 : 0);
    if (name == "Metadata")
        return appendMetadata(reply, snapshot.track);
    if (name == "Volume")
        return sd_bus_message_append(reply, "d", snapshot.volume);
    if (name == "Position")
        return sd_bus_message_append(reply, "x", snapshot.position_us);
    if (name == "MinimumRate")
        return sd_bus_message_append(reply, "d", snapshot.minimum_rate);
    if (name == "MaximumRate")
        return sd_bus_message_append(reply, "d", snapshot.maximum_rate);
    if (name == "CanGoNext")
        return sd_bus_message_append(reply, "b", snapshot.can_go_next ? 1 : 0);
    if (name == "CanGoPrevious")
        return sd_bus_message_append(reply, "b", snapshot.can_go_previous ? 1 : 0);
    if (name == "CanPlay")
        return sd_bus_message_append(reply, "b", snapshot.can_play ? 1 : 0);
    if (name == "CanPause")
        return sd_bus_message_append(reply, "b", snapshot.can_pause ? 1 : 0);
    if (name == "CanSeek")
        return sd_bus_message_append(reply, "b", snapshot.can_seek ? 1 : 0);
    if (name == "CanControl")
        return sd_bus_message_append(reply, "b", snapshot.can_control ? 1 : 0);
    return -EINVAL;
}

int MprisService::setPlayerRate(sd_bus* /*bus*/,
                                const char* /*path*/,
                                const char* /*interface*/,
                                const char* /*property*/,
                                sd_bus_message* value,
                                void* /*userdata*/,
                                sd_bus_error* /*error*/) {
    double rate = 1.0;
    const int ret = sd_bus_message_read(value, "d", &rate);
    if (ret < 0)
        return ret;

    return 1;
}

int MprisService::setPlayerLoopStatus(sd_bus* /*bus*/,
                                      const char* /*path*/,
                                      const char* /*interface*/,
                                      const char* /*property*/,
                                      sd_bus_message* value,
                                      void* userdata,
                                      sd_bus_error* error) {
    const char* loop_status = nullptr;
    int ret = sd_bus_message_read(value, "s", &loop_status);
    if (ret < 0)
        return ret;

    const auto parsed = parseMprisLoopStatus(loop_status);
    if (!parsed) {
        return sd_bus_error_setf(error,
                                 SD_BUS_ERROR_NOT_SUPPORTED,
                                 "Unsupported loop status: %s",
                                 loop_status);
    }

    auto command = makeCommand(MprisCommandType::SetLoopStatus);
    command.loop_status = *parsed;
    static_cast<MprisService*>(userdata)->queueCommand(std::move(command));
    return 1;
}

int MprisService::setPlayerShuffle(sd_bus* /*bus*/,
                                   const char* /*path*/,
                                   const char* /*interface*/,
                                   const char* /*property*/,
                                   sd_bus_message* value,
                                   void* /*userdata*/,
                                   sd_bus_error* error) {
    int shuffle = 0;
    const int ret = sd_bus_message_read(value, "b", &shuffle);
    if (ret < 0)
        return ret;

    if (!shuffle)
        return 1;

    return sd_bus_error_setf(error,
                             SD_BUS_ERROR_NOT_SUPPORTED,
                             "Shuffle mode is not supported");
}

int MprisService::setPlayerVolume(sd_bus* /*bus*/,
                                  const char* /*path*/,
                                  const char* /*interface*/,
                                  const char* /*property*/,
                                  sd_bus_message* value,
                                  void* userdata,
                                  sd_bus_error* /*error*/) {
    double volume = 1.0;
    const int ret = sd_bus_message_read(value, "d", &volume);
    if (ret < 0)
        return ret;

    auto command = makeCommand(MprisCommandType::SetVolume);
    command.volume = std::max(0.0, volume);
    static_cast<MprisService*>(userdata)->queueCommand(std::move(command));
    return 1;
}

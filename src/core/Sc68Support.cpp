#include "Sc68Support.hpp"

#include "core/SupportedFormats.hpp"

#include <sc68/sc68.h>

#include <mutex>
#include <string>

namespace {

std::once_flag g_sc68_init_once;
bool           g_sc68_init_ok = false;
std::string    g_sc68_init_error;

}  // namespace

std::expected<void, std::string> ensureSc68LibraryInitialized() {
    std::call_once(g_sc68_init_once, []() {
        sc68_init_t init{};
        init.flags.no_load_config = 1;
        init.flags.no_save_config = 1;

        if (sc68_init(&init) == 0) {
            g_sc68_init_ok = true;
            return;
        }

        g_sc68_init_error = "Cannot initialize libsc68";
    });

    if (!g_sc68_init_ok)
        return std::unexpected(g_sc68_init_error);

    return {};
}

bool probeSc68File(const std::filesystem::path& path) {
    if (!isSc68Extension(path.extension().string()))
        return false;

    if (auto init = ensureSc68LibraryInitialized(); !init)
        return false;

    const std::string uri = path.string();
    sc68_disk_t disk = sc68_load_disk_uri(uri.c_str());
    if (!disk)
        return false;

    sc68_disk_free(disk);
    return true;
}


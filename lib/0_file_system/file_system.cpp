#include <LittleFS.h>

#include "file_system.h"
#include "logging.h"

namespace file_system
{
    bool is_mounted = false;

    void init()
    {
        log_info("[file_system] Initializing LittleFS...");
        is_mounted = LittleFS.begin(true, MOUNT_POINT, MAX_OPEN_FILES, PARTITION_LABEL);

        if (is_mounted)
        {
            log_info("[file_system] LittleFS mounted successfully.");
        }
        else
        {
            log_info("[file_system] Failed to mount LittleFS.");
        }
    }
}
#pragma once

#include <LittleFS.h>

#include "logging.h"

#define MOUNT_POINT "/littlefs"
#define MAX_OPEN_FILES 10
#define PARTITION_LABEL "spiffs"

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
} // namespace file_system
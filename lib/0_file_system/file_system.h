#pragma once

#define MOUNT_POINT "/littlefs"
#define MAX_OPEN_FILES 10
#define PARTITION_LABEL "spiffs"

namespace file_system
{
    extern bool is_mounted;

    void init();
} // namespace file_system
---
name: "C++ Include Order"
description: "Use when creating or modifying C++ source files. Enforces project header-first include order, angle brackets for core or external libraries, and quotes for project libraries."
applyTo: "**/*.cpp"
---
# C++ Include Order

For every `.cpp` file:

1. Include the header belonging to the source file first, using quotes.
2. Include core, framework, and external library headers next, using angle brackets.
3. Include project or local library headers last, using quotes.
4. Keep the three groups in that order, with a blank line between groups when more than one group is present.

Example:

```cpp
#include "config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "file_system.h"
#include "logging.h"
```

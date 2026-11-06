#pragma once
// Diagnostic: periodic heap + per-task stack report over serial. Temporary —
// remove once the RAM budget is understood. Start once at boot.
void memreport_start();
void memreport_mark(const char* where);

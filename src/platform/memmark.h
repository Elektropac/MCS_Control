#pragma once
// =======================================================
// MEMMARK — heap checkpoints through boot (diagnostics)
// =======================================================
// memmark("label") records free heap + largest free block at that moment.
// Serial command 'h' prints the list with the delta to the previous mark,
// so you can see exactly what each init step costs. ~20 marks x 32 bytes.
#include <Arduino.h>

void memmark(const char* label);
void memmark_print();

#pragma once
// =======================================================
// txn_log — flash-backed store for finished fuellings (append-only ring file)
// =======================================================
// Finished transactions wait here until the server has acknowledged them.
// They survive reboot/power loss and there can be thousands of them (offline
// sites), instead of the old 32-slot RAM queue that was lost on reboot.
//
// File format (/txn.log): one record per line, never rewritten in place:
//     <compact JSON>*<crc32 as 8 hex>\n
//   {"t":"U","id":N}                 id N was handed out (authorize)  -> keeps ids unique across reboots
//   {"t":"S","n":N}                  next id is N (first line after a rotation)
//   {"t":"P","id":N,"ref":..,...}    transaction N finished: full record, awaiting server ACK
//   {"t":"A","id":N}                 transaction N acknowledged by the server
// A line with a bad/missing checksum (e.g. half-written at power loss) is ignored.
//
// Rotation: when the file passes `rotate_bytes`, a new file is written with an
// S record + the P lines of all still-unacked transactions, then swapped in
// (write new -> remove old -> rename). Acked history is dropped.
//
// Flash wear: LittleFS spreads writes over the whole partition (~6 MB). Even
// 1000 fuellings/day (~0.5 MB/day) erases each sector only every ~10 days,
// far below the ~100k erase cycles of the flash.
//
// RAM: only the ids of unacked transactions (4 bytes each) are kept in RAM.
// All functions are thread-safe (internal mutex).
// =======================================================
#include <Arduino.h>
#include <functional>

struct TxnRecord {
    uint32_t id;
    char     ref[40];
    char     pump[16];
    char     group[24];
    char     card[32];      // card numbers are up to 24 digits (overwrite cards)
    char     driver[24];
    char     vehicle[20];
    uint32_t odometer;
    float    preset;
    float    litres;
};

// Replay the file (rebuild pending ids + next id). Safe to call again (tests).
bool txn_log_init(const char* path = "/txn.log", size_t rotate_bytes = 512 * 1024);

// Next transaction id to hand out (one higher than any id ever seen in the log).
uint32_t txn_log_next_id();

// Persist that `id` was handed out (so it is never reused after a reboot).
bool txn_log_note_id(uint32_t id);

// Persist a finished transaction (becomes pending). Returns false on flash error.
bool txn_log_add_pending(const TxnRecord& r);

// Persist the server ACK. Returns true if `id` was pending (false = unknown / already acked).
bool txn_log_ack(uint32_t id);

bool   txn_log_is_pending(uint32_t id);
size_t txn_log_pending_count();
size_t txn_log_file_size();

// Call `fn` for up to `max` pending records, oldest first. Returns how many were visited.
size_t txn_log_for_each_pending(size_t max, const std::function<void(const TxnRecord&)>& fn);

// Self-test on a separate file (/txn_test.log, 4 KB rotation): pending/ack,
// rotation, "reboot" (re-init), damaged last line, rotation crash recovery.
// Prints PASS/FAIL per check and re-opens the real log afterwards.
// Serial command: "txn test". Does not touch /txn.log.
bool txn_log_selftest();

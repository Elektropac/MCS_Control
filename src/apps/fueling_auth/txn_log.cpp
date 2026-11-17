#include "txn_log.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>
#include <algorithm>

#include "logging.h"

static String              s_path = "/txn.log";
static String              s_tmp  = "/txn.new";
static size_t              s_rotate_bytes = 512 * 1024;
static std::vector<uint32_t> s_pending;        // unacked ids, oldest first
static uint32_t            s_max_id = 0;
static SemaphoreHandle_t   s_mutex = nullptr;

static const size_t LINE_MAX_LEN = 400;          // a P record is ~200 bytes

struct Lock {
    Lock()  { if (s_mutex) xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
    ~Lock() { if (s_mutex) xSemaphoreGiveRecursive(s_mutex); }
};

// ---------- line encode / decode ----------
static uint32_t crc_of(const char* s, size_t n) {
    return esp_rom_crc32_le(0, (const uint8_t*)s, n);
}

// Build "<json>*<crc>\n" from a document.
static String encode(JsonDocument& d) {
    String body;
    serializeJson(d, body);
    char tail[12];
    snprintf(tail, sizeof(tail), "*%08lx\n", (unsigned long)crc_of(body.c_str(), body.length()));
    return body + tail;
}

// Validate + parse one line (without '\n'). Returns false if damaged.
static bool decode(char* line, size_t len, JsonDocument& out) {
    while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) line[--len] = '\0';
    if (len < 10) return false;
    char* star = (char*)memrchr(line, '*', len);
    if (!star || (size_t)(line + len - star) != 9) return false;
    uint32_t want = strtoul(star + 1, nullptr, 16);
    size_t body_len = star - line;
    if (crc_of(line, body_len) != want) return false;
    *star = '\0';
    return deserializeJson(out, line, body_len) == DeserializationError::Ok;
}

static void record_to_doc(const TxnRecord& r, JsonDocument& d) {
    d["t"] = "P";
    d["id"] = r.id;
    d["ref"] = r.ref;
    d["pump"] = r.pump;
    d["group"] = r.group;
    if (r.card[0])    d["card"] = r.card;
    if (r.driver[0])  d["driver"] = r.driver;
    if (r.vehicle[0]) d["vehicle"] = r.vehicle;
    if (r.odometer)   d["odo"] = r.odometer;
    d["preset"] = r.preset;
    d["litres"] = r.litres;
}

static void copy_str(JsonDocument& d, const char* key, char* dst, size_t n) {
    const char* v = d[key] | "";
    strncpy(dst, v, n - 1);
    dst[n - 1] = '\0';
}

static void doc_to_record(JsonDocument& d, TxnRecord& r) {
    memset(&r, 0, sizeof(r));
    r.id = d["id"] | 0u;
    copy_str(d, "ref", r.ref, sizeof(r.ref));
    copy_str(d, "pump", r.pump, sizeof(r.pump));
    copy_str(d, "group", r.group, sizeof(r.group));
    copy_str(d, "card", r.card, sizeof(r.card));
    copy_str(d, "driver", r.driver, sizeof(r.driver));
    copy_str(d, "vehicle", r.vehicle, sizeof(r.vehicle));
    r.odometer = d["odo"] | 0u;
    r.preset = d["preset"] | 0.0f;
    r.litres = d["litres"] | 0.0f;
}

// Read the file line by line, calling fn(doc) for every valid record.
static void scan(const String& path, const std::function<bool(JsonDocument&, const char* raw, size_t raw_len)>& fn) {
    if (!LittleFS.exists(path)) return;          // (open "r" on a missing file logs an error)
    File f = LittleFS.open(path, "r");
    if (!f) return;
    char buf[LINE_MAX_LEN];
    while (f.available()) {
        size_t n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
        buf[n] = '\0';
        char raw[LINE_MAX_LEN];
        memcpy(raw, buf, n + 1);                 // decode() modifies buf
        JsonDocument d;
        if (!decode(buf, n, d)) continue;         // damaged / partial line -> skip
        if (!fn(d, raw, n)) break;
    }
    f.close();
}

static bool append_line(const String& line) {
    File f = LittleFS.open(s_path, "a");
    if (!f) return false;
    size_t w = f.print(line);
    f.close();
    return w == line.length();
}

static void remove_pending(uint32_t id) {
    auto it = std::find(s_pending.begin(), s_pending.end(), id);
    if (it != s_pending.end()) s_pending.erase(it);
}

// Write a fresh file with only what is still needed, then swap it in.
static void rotate() {
    File out = LittleFS.open(s_tmp, "w");
    if (!out) { log_error("[txn_log] rotate: cannot create %s", s_tmp.c_str()); return; }
    {
        JsonDocument s; s["t"] = "S"; s["n"] = s_max_id + 1;
        out.print(encode(s));
    }
    size_t copied = 0;
    bool ok = true;
    scan(s_path, [&](JsonDocument& d, const char* raw, size_t raw_len) {
        const char* t = d["t"] | "";
        if (strcmp(t, "P") == 0 && txn_log_is_pending(d["id"] | 0u)) {
            if (out.write((const uint8_t*)raw, raw_len) != raw_len || out.write('\n') != 1) ok = false;
            copied++;
        }
        return true;
    });
    out.close();
    if (!ok || copied != s_pending.size()) {
        // Never swap in an incomplete file: keep the old one (it is still valid).
        log_error("[txn_log] rotate aborted (copied %u of %u pending)", (unsigned)copied, (unsigned)s_pending.size());
        LittleFS.remove(s_tmp);
        return;
    }
    LittleFS.remove(s_path);
    LittleFS.rename(s_tmp, s_path);
    log_info("[txn_log] rotated: %u pending kept", (unsigned)copied);
}

static void rotate_if_needed() {
    if (txn_log_file_size() >= s_rotate_bytes) rotate();
}

// ---------- public ----------
bool txn_log_init(const char* path, size_t rotate_bytes) {
    if (!s_mutex) s_mutex = xSemaphoreCreateRecursiveMutex();
    Lock lock;
    s_path = path;
    s_tmp = String(path) + ".new";
    s_rotate_bytes = rotate_bytes;
    s_pending.clear();
    s_max_id = 0;

    // Crash recovery for a rotation: new file written but not yet renamed.
    bool has_main = LittleFS.exists(s_path);
    bool has_tmp  = LittleFS.exists(s_tmp);
    if (!has_main && has_tmp) LittleFS.rename(s_tmp, s_path);   // old was already removed -> new is complete
    else if (has_tmp)         LittleFS.remove(s_tmp);            // old still there -> new may be partial

    // Power loss can leave the last line without its '\n'. Terminate it now,
    // otherwise the NEXT record would be glued onto the damaged line and fail
    // its checksum too — i.e. the first transaction after a power cut would be
    // lost. (Found by the self-test.)
    if (LittleFS.exists(s_path)) {
        File f = LittleFS.open(s_path, "r");
        size_t sz = f ? f.size() : 0;
        bool needs_nl = false;
        if (f && sz > 0) { f.seek(sz - 1); needs_nl = (f.read() != '\n'); }
        if (f) f.close();
        if (needs_nl) {
            File a = LittleFS.open(s_path, "a");
            if (a) { a.print('\n'); a.close(); }
            log_info("[txn_log] terminated a damaged last line (power loss?)");
        }
    }

    size_t valid = 0;
    scan(s_path, [&](JsonDocument& d, const char*, size_t) {
        valid++;
        const char* t = d["t"] | "";
        uint32_t id = d["id"] | 0u;
        if (!strcmp(t, "S")) { uint32_t n = d["n"] | 1u; if (n > 0 && n - 1 > s_max_id) s_max_id = n - 1; }
        else if (!strcmp(t, "U")) { if (id > s_max_id) s_max_id = id; }
        else if (!strcmp(t, "P")) { if (id > s_max_id) s_max_id = id; if (!txn_log_is_pending(id)) s_pending.push_back(id); }
        else if (!strcmp(t, "A")) { remove_pending(id); }
        return true;
    });
    log_info("[txn_log] %s: %u records, %u pending, next id %lu, %u bytes",
             s_path.c_str(), (unsigned)valid, (unsigned)s_pending.size(),
             (unsigned long)(s_max_id + 1), (unsigned)txn_log_file_size());
    return true;
}

uint32_t txn_log_next_id() { Lock lock; return s_max_id + 1; }

bool txn_log_note_id(uint32_t id) {
    Lock lock;
    if (id > s_max_id) s_max_id = id;
    JsonDocument d; d["t"] = "U"; d["id"] = id;
    rotate_if_needed();
    return append_line(encode(d));
}

bool txn_log_add_pending(const TxnRecord& r) {
    Lock lock;
    JsonDocument d;
    record_to_doc(r, d);
    String line = encode(d);
    if (line.length() >= LINE_MAX_LEN) {
        log_error("[txn_log] record %lu too long (%u)", (unsigned long)r.id, (unsigned)line.length());
        return false;
    }
    rotate_if_needed();
    if (!append_line(line)) return false;
    if (r.id > s_max_id) s_max_id = r.id;
    if (!txn_log_is_pending(r.id)) s_pending.push_back(r.id);
    return true;
}

bool txn_log_ack(uint32_t id) {
    Lock lock;
    if (!txn_log_is_pending(id)) return false;
    JsonDocument d; d["t"] = "A"; d["id"] = id;
    rotate_if_needed();
    if (!append_line(encode(d))) return false;   // stays pending; the server will ack again
    remove_pending(id);
    return true;
}

bool txn_log_is_pending(uint32_t id) {
    Lock lock;
    return std::find(s_pending.begin(), s_pending.end(), id) != s_pending.end();
}

size_t txn_log_pending_count() { Lock lock; return s_pending.size(); }

size_t txn_log_file_size() {
    if (!LittleFS.exists(s_path)) return 0;
    File f = LittleFS.open(s_path, "r");
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
}

size_t txn_log_for_each_pending(size_t max, const std::function<void(const TxnRecord&)>& fn) {
    Lock lock;
    if (s_pending.empty() || max == 0) return 0;
    size_t visited = 0;
    std::vector<uint32_t> seen;                       // a P line could in theory appear twice
    scan(s_path, [&](JsonDocument& d, const char*, size_t) {
        const char* t = d["t"] | "";
        if (strcmp(t, "P") != 0) return true;
        uint32_t id = d["id"] | 0u;
        if (!txn_log_is_pending(id) || std::find(seen.begin(), seen.end(), id) != seen.end()) return true;
        seen.push_back(id);
        TxnRecord r;
        doc_to_record(d, r);
        fn(r);
        visited++;
        return visited < max && visited < s_pending.size();
    });
    return visited;
}

// ---------- self-test ----------
static int s_fail = 0;
static void check(bool ok, const char* what) {
    Serial.printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) s_fail++;
}

static TxnRecord make_rec(uint32_t id) {
    TxnRecord r;
    memset(&r, 0, sizeof(r));
    r.id = id;
    snprintf(r.ref, sizeof(r.ref), "TEST-%lu", (unsigned long)id);
    strcpy(r.pump, "Pump 1");
    strcpy(r.group, "fueling_1");
    strcpy(r.card, "777777001233000000005678");
    strcpy(r.driver, "Driver");
    strcpy(r.vehicle, "AB12345");
    r.odometer = 123456;
    r.preset = 50.0f;
    r.litres = id * 1.25f;
    return r;
}

bool txn_log_selftest() {
    const char* P = "/txn_test.log";
    String real_path = s_path;
    size_t real_rotate = s_rotate_bytes;
    s_fail = 0;
    Serial.println("txn_log selftest");
    if (LittleFS.exists(P)) LittleFS.remove(P);
    if (LittleFS.exists(String(P) + ".new")) LittleFS.remove(String(P) + ".new");

    txn_log_init(P, 4096);
    check(txn_log_pending_count() == 0 && txn_log_next_id() == 1, "empty log: 0 pending, next id 1");

    // 30 fuellings, ack every third -> 20 pending. 30 P lines (~200 B) force rotations at 4 KB.
    for (uint32_t id = 1; id <= 30; id++) {
        txn_log_note_id(id);
        txn_log_add_pending(make_rec(id));
        if (id % 3 == 0) txn_log_ack(id);
    }
    check(txn_log_pending_count() == 20, "30 added, 10 acked -> 20 pending");
    check(txn_log_file_size() < 4096 + LINE_MAX_LEN * 2 + 4096, "file stays bounded (rotation ran)");
    check(!txn_log_ack(3), "ack of already-acked id is a no-op");
    check(!txn_log_ack(999), "ack of unknown id is a no-op");

    // "Reboot": replay from flash.
    txn_log_init(P, 4096);
    check(txn_log_pending_count() == 20, "after re-init: still 20 pending");
    check(txn_log_next_id() == 31, "after re-init: next id 31 (no reuse)");

    uint32_t first = 0, prev = 0; size_t n = 0; bool ordered = true, fields = true;
    txn_log_for_each_pending(1000, [&](const TxnRecord& r) {
        if (!first) first = r.id;
        if (r.id <= prev) ordered = false;
        prev = r.id; n++;
        TxnRecord e = make_rec(r.id);
        if (strcmp(r.ref, e.ref) || strcmp(r.card, e.card) || r.odometer != e.odometer ||
            fabsf(r.litres - e.litres) > 0.001f || strcmp(r.vehicle, e.vehicle)) fields = false;
    });
    check(n == 20 && first == 1 && ordered, "for_each: 20 records, oldest first");
    check(fields, "for_each: all fields round-trip");
    size_t lim = txn_log_for_each_pending(5, [](const TxnRecord&) {});
    check(lim == 5, "for_each respects max");

    // Damaged last line (power loss mid-write) must be ignored.
    { File f = LittleFS.open(P, "a"); f.print("{\"t\":\"A\",\"id\":1}*dead"); f.close(); }
    txn_log_init(P, 4096);
    check(txn_log_pending_count() == 20 && txn_log_is_pending(1), "half-written line ignored");
    txn_log_add_pending(make_rec(40));      // appending after a damaged tail still works
    txn_log_init(P, 4096);
    check(txn_log_is_pending(40) && txn_log_pending_count() == 21, "record after damaged line is read");

    // Rotation crash: new file complete, old already removed -> new is adopted.
    { File f = LittleFS.open(P, "r"); String all = f.readString(); f.close();
      File w = LittleFS.open(String(P) + ".new", "w"); w.print(all); w.close();
      LittleFS.remove(P); }
    txn_log_init(P, 4096);
    check(txn_log_pending_count() == 21 && LittleFS.exists(P), "crash after remove: .new adopted");
    // Rotation crash: partial new file next to the old -> new discarded.
    { File w = LittleFS.open(String(P) + ".new", "w"); w.print("{\"t\":\"S\""); w.close(); }
    txn_log_init(P, 4096);
    check(txn_log_pending_count() == 21 && !LittleFS.exists(String(P) + ".new"), "partial .new discarded");

    // Ack all -> empty; next id survives a rotation that drops every P line.
    std::vector<uint32_t> ids = s_pending;
    for (uint32_t id : ids) txn_log_ack(id);
    check(txn_log_pending_count() == 0, "all acked -> 0 pending");
    rotate();
    txn_log_init(P, 4096);
    check(txn_log_pending_count() == 0 && txn_log_next_id() == 41, "after rotation of empty log: next id kept (41)");

    LittleFS.remove(P);
    txn_log_init(real_path.c_str(), real_rotate);   // back to the real log
    Serial.printf("txn_log selftest: %s (%d failed)\n", s_fail ? "FAILED" : "ALL PASSED", s_fail);
    return s_fail == 0;
}

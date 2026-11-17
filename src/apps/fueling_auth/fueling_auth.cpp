#include "fueling_auth.h"

#include "logging.h"
#include "encryption.h"          // Encrypt/Decrypt (AES-256-GCM, lib/encryption)
#include "apps/pumps/pumps.h"
#include "apps/rule_engine/rule_engine.h"
#include "my_wifi.h"             // wifi::make_mac()
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "txn_log.h"

// =======================================================
// Configuration / tunables
// =======================================================

// ⚠️ PLACEHOLDER shared master secret. MUST be replaced with the real value
// (and ideally moved out of the firmware image into NVS/efuse) before field
// use. Same value on all devices (app/Console/server share it); device_key is
// made device-unique by mixing in the MAC. See the design note.
static const char* MCS_MASTER_SECRET = "CHANGE-ME-MCS-MASTER-SECRET-v0";

// Current key version. Lets us rotate MCS_MASTER_SECRET later without bricking
// the fleet: the sender states which version it used; we only accept known
// versions. Bump when the secret rotates.
static const uint8_t KEY_VERSION = 1;

// How long an authorization waits for the nozzle to be lifted before it is
// auto-cancelled and the pump freed.
static const uint32_t AUTH_TIMEOUT_MS = 60000;

// Max transactions not yet acknowledged by the server (authorized + finished).
// When reached, new authorizations are refused (measurement-critical: never
// drop one). Finished ones live in flash (txn_log); RAM cost is 4 bytes per
// pending id, so 1000 = 4 KB worst case, only while that many are offline.
static const uint16_t TXN_QUEUE_MAX = 1000;

// Max transactions returned by one txn_pending call. Same as the old queue
// size, so the reply never gets bigger than before: ack these, ask again.
static const uint8_t TXN_PENDING_LIST_MAX = 32;

// =======================================================
// Transaction store
// =======================================================
// RAM: only transactions that are AUTHORIZED right now (at most one per pump),
// plus — only if a flash write failed — finished ones that could not be saved.
// Finished transactions go to flash (txn_log, append-only ring file) and stay
// there until the server ACKs them: they survive reboot/power loss.
enum TxnStatus : uint8_t { TXN_FREE = 0, TXN_AUTHORIZED, TXN_PENDING_RAM };
static const uint8_t TXN_ACTIVE_MAX = 4;   // = MAX_PUMPS in pumps.cpp

struct Txn {
    uint32_t id;            // internal lookup id (device-local sequence)
    char     ref[40];       // globally-unique transaction ref: "<MAC>-<seq>"
    char     pump[16];      // display name (e.g. "Pump 1")
    char     group[24];     // rule group id (e.g. "fueling_1")
    char     card[32];      // access medium / card id (NOT the PIN). Was 20: cut 24-digit card numbers to 19
    char     driver[24];    // driver id / name
    char     vehicle[20];   // vehicle / number plate
    uint32_t odometer;      // odometer reading (0 = not given)
    float    preset;        // 0 = no preset
    float    litres;        // final/running volume
    TxnStatus status;
    uint32_t auth_ms;       // when authorized (for auth-timeout)
};

static Txn      s_txns[TXN_ACTIVE_MAX];
static uint8_t  s_txn_count = 0;        // RAM slots in use (not FREE)
static uint32_t s_next_id   = 1;        // seeded from txn_log at init (unique across reboots)

// The store is used from the rule-engine task (complete) and the web/BLE
// tasks (authorize/pending/ack) — serialise access.
static SemaphoreHandle_t s_lock = nullptr;
struct StoreLock {
    StoreLock()  { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~StoreLock() { if (s_lock) xSemaphoreGiveRecursive(s_lock); }
};

// Nonce issued by auth_challenge, consumed by the next authorize. 0 = none.
static uint32_t s_nonce = 0;

// -------------------------------------------------------
static Txn* find_txn(uint32_t id) {
    for (auto& t : s_txns) if (t.status != TXN_FREE && t.id == id) return &t;
    return nullptr;
}
// Does this pump have an ACTIVE (authorised, not-yet-fuelled) transaction?
// A PENDING/SENT txn is a FINISHED fuelling awaiting delivery to the server —
// it does NOT occupy the pump, so a new fuelling can start immediately. Only an
// AUTHORIZED txn (someone currently holds the pump) blocks a new authorization.
static Txn* find_active_pump_txn(const char* pump) {
    for (auto& t : s_txns)
        if (t.status == TXN_AUTHORIZED && strcmp(t.pump, pump) == 0) return &t;
    return nullptr;
}

// Reconcile the auth store with the rule-engine's transaction state. The engine
// owns the live fuelling state machine; it can move a pump back to "idle" on an
// auth-timeout / no-flow-timeout WITHOUT telling us. An AUTHORIZED txn whose
// pump is no longer authorised/pumping in the engine is stale (it timed out and
// never fuelled) -> free the slot so the pump isn't seen as occupied forever.
// (Txns that actually fuelled become PENDING elsewhere and are NOT freed here;
// those must still be delivered + acked.)
static void reconcile_with_engine() {
    JsonDocument doc;
    if (deserializeJson(doc, pumps_get_all()) != DeserializationError::Ok) return;
    for (auto& t : s_txns) {
        if (t.status != TXN_AUTHORIZED) continue;
        for (JsonObject p : doc["pumps"].as<JsonArray>()) {
            if (strcmp(p["name"] | "", t.pump) != 0) continue;
            const char* st = p["state"] | "idle";
            // Still live in the engine? leave it. Back to idle/done? it timed
            // out before fuelling -> free the slot.
            if (strcmp(st, "authorised") != 0 && strcmp(st, "pumping") != 0) {
                log_info("[fueling_auth] txn %lu freed (engine state '%s')", (unsigned long)t.id, st);
                t.status = TXN_FREE;
                s_txn_count--;
            }
            break;
        }
    }
}
static uint32_t count_unacked() {
    uint32_t n = txn_log_pending_count();
    for (auto& t : s_txns)
        if (t.status == TXN_AUTHORIZED || t.status == TXN_PENDING_RAM) n++;
    return n;
}
static Txn* alloc_txn() {
    for (auto& t : s_txns) if (t.status == TXN_FREE) return &t;
    return nullptr;
}

// =======================================================
// device_key = master secret + MAC + key version (string fed to Encrypt/Decrypt,
// which SHA256-hashes it into the AES-256 key).
// =======================================================
static String device_key() {
    String mac = wifi::make_mac();          // from eFuse; stable before WiFi up
    return String(MCS_MASTER_SECRET) + ":" + mac + ":v" + String(KEY_VERSION);
}

// =======================================================
// Helpers
// =======================================================
static String err(int code, const char* text) {
    JsonDocument d;
    d["error"] = true;
    d["code"] = code;
    d["text"] = text;
    String s; serializeJson(d, s); return s;
}

// Copy a JSON string field into a fixed buffer (safe, null-terminated).
static void copy_field(JsonDocument& o, const char* key, char* dst, size_t n) {
    const char* v = o[key] | "";
    strncpy(dst, v, n - 1);
    dst[n - 1] = '\0';
}

// Does a pump of this name exist in the live config?
static bool pump_exists(const char* name) {
    JsonDocument doc;
    deserializeJson(doc, pumps_get_all());
    for (JsonObject p : doc["pumps"].as<JsonArray>())
        if (strcmp(p["name"] | "", name) == 0) return true;
    return false;
}

// Resolve a pump's rule-group id (e.g. "fueling_1") from its display name.
// Returns "" if not found. Used to link a transaction to its engine group.
static String group_of_pump(const char* name) {
    JsonDocument doc;
    deserializeJson(doc, pumps_get_all());
    for (JsonObject p : doc["pumps"].as<JsonArray>())
        if (strcmp(p["name"] | "", name) == 0) return String(p["id"] | "");
    return String("");
}

// =======================================================
// Public API
// =======================================================
void fueling_auth_init() {
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
    StoreLock lock;
    for (auto& t : s_txns) t.status = TXN_FREE;
    s_txn_count = 0;
    txn_log_init();                       // replays /txn.log: pending txns + last id
    s_next_id = txn_log_next_id();
    log_info("[fueling_auth] init (key v%u, queue max %u, %u pending in flash, next id %lu) — PLACEHOLDER secret",
             KEY_VERSION, TXN_QUEUE_MAX, (unsigned)txn_log_pending_count(), (unsigned long)s_next_id);
}

bool fueling_auth_queue_full() {
    StoreLock lock;
    return count_unacked() >= TXN_QUEUE_MAX;
}

String fueling_auth_challenge() {
    // Fresh 32-bit nonce. Non-zero.
    do { s_nonce = esp_random(); } while (s_nonce == 0);
    JsonDocument d;
    d["nonce"] = s_nonce;
    String s; serializeJson(d, s); return s;
}

String fueling_auth_authorize(JsonObject data) {
    StoreLock lock;
    // 0) Reconcile with the engine so timed-out authorizations don't leave a
    //    pump looking occupied forever.
    reconcile_with_engine();

    // 1) Queue full? Refuse everything (checked first).
    if (fueling_auth_queue_full())
        return err(51, "transaction queue full");

    // 2) Authenticate: the request carries an encrypted "auth" hex blob made
    //    with device_key, wrapping {pump, preset?, nonce, key_ver}.
    if (data["auth"].isNull())
        return err(75, "missing auth blob");
    String blob = data["auth"].as<String>();
    String plain = Decrypt(blob, device_key());
    if (plain.length() == 0)
        return err(71, "auth failed (bad key or tampered)");

    JsonDocument inner;
    if (deserializeJson(inner, plain) != DeserializationError::Ok)
        return err(75, "auth payload not JSON");

    // key version must be one we accept
    uint8_t kv = inner["key_ver"] | 0;
    if (kv != KEY_VERSION)
        return err(71, "unknown key version");

    // 3) Anti-replay: nonce must match the one we just issued, then burn it.
    uint32_t nonce = inner["nonce"] | 0u;
    if (s_nonce == 0 || nonce != s_nonce)
        return err(71, "bad or expired nonce");
    s_nonce = 0;   // one-time use

    const char* pump = inner["pump"] | "";
    float preset = inner["preset"] | 0.0f;
    if (!pump[0]) return err(4, "pump missing");

    // 4) Pump exists / configured?
    if (!pump_exists(pump)) return err(20, "unknown pump");

    // 5) Pump occupied by an ACTIVE authorization? (A finished-but-undelivered
    //    PENDING txn does NOT occupy the pump — a new fuelling can start.)
    if (find_active_pump_txn(pump)) return err(70, "pump occupied");

    // All checks passed — create the transaction.
    Txn* t = alloc_txn();
    if (!t) return err(51, "no free slot");
    t->id = s_next_id++;
    // Persist that this id is taken, so it is never reused after a reboot
    // (the server de-duplicates on txn_ref = MAC-id).
    if (!txn_log_note_id(t->id)) log_error("[fueling_auth] could not persist id %lu", (unsigned long)t->id);
    // Globally-unique ref = device MAC (no colons) + local sequence, so the
    // server can tell transactions from different devices apart.
    {
        String mac = wifi::make_mac(); mac.replace(":", "");
        snprintf(t->ref, sizeof(t->ref), "%s-%u", mac.c_str(), (unsigned)t->id);
    }
    strncpy(t->pump, pump, sizeof(t->pump) - 1);
    t->pump[sizeof(t->pump) - 1] = '\0';
    String gid = group_of_pump(pump);
    strncpy(t->group, gid.c_str(), sizeof(t->group) - 1);
    t->group[sizeof(t->group) - 1] = '\0';
    // Transaction metadata from the (authenticated) request. PIN is deliberately
    // NOT stored/transmitted — it's only for app-side access validation.
    copy_field(inner, "card",    t->card,    sizeof(t->card));
    copy_field(inner, "driver",  t->driver,  sizeof(t->driver));
    copy_field(inner, "vehicle", t->vehicle, sizeof(t->vehicle));
    t->odometer = inner["odometer"] | 0u;
    t->preset = preset;
    t->litres = 0.0f;
    t->status = TXN_AUTHORIZED;
    t->auth_ms = millis();
    s_txn_count++;

    // Hand the authorization to the rule-engine (it owns the relay + the
    // nozzle-lift/flow/preset-stop rules). Reuse the existing comm-message hook
    // and KV state the fueling rules already listen on.
    rule_kv_set_num((String("preset_") + pump).c_str(), preset);
    rule_kv_set_num((String("txn_") + pump).c_str(), (double)t->id);
    rule_engine_feed_message((String("AUTH_OK:") + pump).c_str());

    log_info("[fueling_auth] authorized txn %lu pump %s preset %.1f",
             (unsigned long)t->id, pump, preset);

    JsonDocument d;
    d["ok"] = true;
    d["txn_id"] = t->id;
    d["txn_ref"] = t->ref;
    d["pump"] = pump;
    String s; serializeJson(d, s); return s;
}

String fueling_auth_cancel(JsonObject data) {
    StoreLock lock;
    uint32_t id = data["txn_id"] | 0u;
    Txn* t = find_txn(id);
    if (!t) return err(71, "unknown txn");
    if (t->status != TXN_AUTHORIZED)
        return err(3, "txn already fuelling / delivered");
    rule_engine_feed_message((String("CANCEL:") + t->pump).c_str());
    t->status = TXN_FREE;
    s_txn_count--;
    log_info("[fueling_auth] cancelled txn %lu", (unsigned long)id);
    return "{\"ok\":true}";
}

// Fill one JSON entry of the txn_pending reply (same fields as before).
static void pending_to_json(JsonObject o, uint32_t id, const char* ref, const char* pump,
                            float litres, float preset, const char* card,
                            const char* driver, const char* vehicle, uint32_t odometer) {
    o["txn_id"]   = id;
    o["txn_ref"]  = ref;
    o["pump"]     = pump;
    o["litres"]   = serialized(String(litres, 2));
    o["preset"]   = serialized(String(preset, 1));
    if (card[0])    o["card"]     = card;
    if (driver[0])  o["driver"]   = driver;
    if (vehicle[0]) o["vehicle"]  = vehicle;
    if (odometer)   o["odometer"] = odometer;
}

String fueling_auth_pending() {
    StoreLock lock;
    JsonDocument d;
    JsonArray arr = d.to<JsonArray>();
    size_t n = 0;
    // Unsaved (flash write failed) first — they are the most at risk.
    for (auto& t : s_txns) {
        if (t.status != TXN_PENDING_RAM || n >= TXN_PENDING_LIST_MAX) continue;
        pending_to_json(arr.add<JsonObject>(), t.id, t.ref, t.pump, t.litres, t.preset,
                        t.card, t.driver, t.vehicle, t.odometer);
        n++;
    }
    // Then the oldest from flash. At most TXN_PENDING_LIST_MAX per call: the
    // caller acks them and asks again until the list is empty.
    txn_log_for_each_pending(TXN_PENDING_LIST_MAX - n, [&](const TxnRecord& r) {
        pending_to_json(arr.add<JsonObject>(), r.id, r.ref, r.pump, r.litres, r.preset,
                        r.card, r.driver, r.vehicle, r.odometer);
    });
    String s; serializeJson(d, s); return s;
}

String fueling_auth_ack(JsonObject data) {
    StoreLock lock;
    uint32_t id = data["txn_id"] | 0u;
    for (auto& t : s_txns) {
        if (t.status == TXN_PENDING_RAM && t.id == id) {
            t.status = TXN_FREE;
            s_txn_count--;
            log_info("[fueling_auth] acked txn %lu (was RAM-only)", (unsigned long)id);
            return "{\"ok\":true}";
        }
    }
    // Idempotent: unknown / already acked == done.
    if (txn_log_ack(id)) log_info("[fueling_auth] acked txn %lu", (unsigned long)id);
    return "{\"ok\":true}";
}

void fueling_auth_complete(const char* group_id, float litres) {
    if (!group_id || !group_id[0]) return;
    StoreLock lock;
    for (auto& t : s_txns) {
        if (t.status != TXN_AUTHORIZED) continue;
        if (strcmp(t.group, group_id) != 0) continue;
        t.litres = litres;

        TxnRecord r;
        memset(&r, 0, sizeof(r));
        r.id = t.id;
        // (strlcpy: safe even if the two structs' field sizes ever differ)
        strlcpy(r.ref, t.ref, sizeof(r.ref));
        strlcpy(r.pump, t.pump, sizeof(r.pump));
        strlcpy(r.group, t.group, sizeof(r.group));
        strlcpy(r.card, t.card, sizeof(r.card));
        strlcpy(r.driver, t.driver, sizeof(r.driver));
        strlcpy(r.vehicle, t.vehicle, sizeof(r.vehicle));
        r.odometer = t.odometer;
        r.preset = t.preset;
        r.litres = litres;

        if (txn_log_add_pending(r)) {
            t.status = TXN_FREE;         // safe in flash now -> free the RAM slot
            s_txn_count--;
            log_info("[fueling_auth] txn %lu complete on %s: %.2f L -> pending (flash)",
                     (unsigned long)t.id, group_id, litres);
        } else {
            // Never drop a measurement: keep it in RAM, still listed + ackable.
            t.status = TXN_PENDING_RAM;
            log_error("[fueling_auth] txn %lu: flash write FAILED, kept in RAM only", (unsigned long)t.id);
            event_log_add("ALARM txn %lu not saved to flash (%.2f L)", (unsigned long)t.id, litres);
        }
        return;
    }
    // No matching authorized txn: a fuelling happened without going through
    // fuel_authorize (e.g. legacy/local). Nothing to record here.
    log_info("[fueling_auth] complete on %s but no open txn (%.2f L)",
             group_id, litres);
}

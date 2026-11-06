#include "fueling_auth.h"

#include "logging.h"
#include "encryption.h"          // Encrypt/Decrypt (AES-256-GCM, lib/encryption)
#include "apps/pumps/pumps.h"
#include "apps/rule_engine/rule_engine.h"
#include "my_wifi.h"             // wifi::make_mac()
#include <esp_random.h>

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

// Max transactions held awaiting server ACK. When this many are unacked, new
// authorizations are refused (measurement-critical: never drop one). Kept
// small while the store is in RAM; grows when flash-backed (see TODO).
static const uint8_t TXN_QUEUE_MAX = 32;

// =======================================================
// Transaction store (RAM for now — TODO: flash-backed, survive reboot)
// =======================================================
enum TxnStatus : uint8_t { TXN_FREE = 0, TXN_AUTHORIZED, TXN_PENDING, TXN_SENT, TXN_ACKED };

struct Txn {
    uint32_t id;            // unique, device-owned
    char     pump[16];
    float    preset;        // 0 = no preset
    float    litres;        // final/running volume
    TxnStatus status;
    uint32_t auth_ms;       // when authorized (for auth-timeout)
};

static Txn      s_txns[TXN_QUEUE_MAX];
static uint8_t  s_txn_count = 0;        // slots in use (not FREE)
static uint32_t s_next_id   = 1;        // TODO: seed from MAC + persisted seq

// Nonce issued by auth_challenge, consumed by the next authorize. 0 = none.
static uint32_t s_nonce = 0;

// -------------------------------------------------------
static Txn* find_txn(uint32_t id) {
    for (auto& t : s_txns) if (t.status != TXN_FREE && t.id == id) return &t;
    return nullptr;
}
static Txn* find_pump_txn(const char* pump) {
    for (auto& t : s_txns)
        if (t.status != TXN_FREE && strcmp(t.pump, pump) == 0) return &t;
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
                log_info("[fueling_auth] txn %u freed (engine state '%s')", t.id, st);
                t.status = TXN_FREE;
                s_txn_count--;
            }
            break;
        }
    }
}
static uint8_t count_unacked() {
    uint8_t n = 0;
    for (auto& t : s_txns)
        if (t.status == TXN_AUTHORIZED || t.status == TXN_PENDING ||
            t.status == TXN_SENT) n++;
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

// Does a pump of this name exist in the live config?
static bool pump_exists(const char* name) {
    JsonDocument doc;
    deserializeJson(doc, pumps_get_all());
    for (JsonObject p : doc["pumps"].as<JsonArray>())
        if (strcmp(p["name"] | "", name) == 0) return true;
    return false;
}

// =======================================================
// Public API
// =======================================================
void fueling_auth_init() {
    for (auto& t : s_txns) t.status = TXN_FREE;
    s_txn_count = 0;
    log_info("[fueling_auth] init (key v%u, queue max %u) — PLACEHOLDER secret",
             KEY_VERSION, TXN_QUEUE_MAX);
    // TODO: load persisted unacked transactions from flash + seed s_next_id.
}

bool fueling_auth_queue_full() {
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

    // 5) Pump occupied?  6) Any unfinished txn on this pump?
    if (find_pump_txn(pump)) return err(70, "pump occupied");

    // All checks passed — create the transaction.
    Txn* t = alloc_txn();
    if (!t) return err(51, "no free slot");
    t->id = s_next_id++;
    strncpy(t->pump, pump, sizeof(t->pump) - 1);
    t->pump[sizeof(t->pump) - 1] = '\0';
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

    log_info("[fueling_auth] authorized txn %u pump %s preset %.1f",
             t->id, pump, preset);

    JsonDocument d;
    d["ok"] = true;
    d["txn_id"] = t->id;
    d["pump"] = pump;
    String s; serializeJson(d, s); return s;
}

String fueling_auth_cancel(JsonObject data) {
    uint32_t id = data["txn_id"] | 0u;
    Txn* t = find_txn(id);
    if (!t) return err(71, "unknown txn");
    if (t->status != TXN_AUTHORIZED)
        return err(3, "txn already fuelling / delivered");
    rule_engine_feed_message((String("CANCEL:") + t->pump).c_str());
    t->status = TXN_FREE;
    s_txn_count--;
    log_info("[fueling_auth] cancelled txn %u", id);
    return "{\"ok\":true}";
}

String fueling_auth_pending() {
    JsonDocument d;
    JsonArray arr = d.to<JsonArray>();
    for (auto& t : s_txns) {
        if (t.status == TXN_PENDING || t.status == TXN_SENT) {
            JsonObject o = arr.add<JsonObject>();
            o["txn_id"] = t.id;
            o["pump"] = t.pump;
            o["litres"] = t.litres;
        }
    }
    String s; serializeJson(d, s); return s;
}

String fueling_auth_ack(JsonObject data) {
    uint32_t id = data["txn_id"] | 0u;
    Txn* t = find_txn(id);
    if (!t) return "{\"ok\":true}";   // idempotent: unknown == already done
    t->status = TXN_ACKED;
    t->status = TXN_FREE;             // acked -> free the slot
    s_txn_count--;
    log_info("[fueling_auth] acked txn %u", id);
    // TODO: persist the ack so a reboot doesn't resurrect the transaction.
    return "{\"ok\":true}";
}

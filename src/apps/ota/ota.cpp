// =======================================================
// OTA — over-the-air firmware update (ported, RTOS + fueling-safe)
// =======================================================
// Ported from the legacy control firmware's ota_manager. Changes:
//   - Plain HTTP only. The active OTA server is http://52.56.195.98/ota/control.
//     All Tailscale Funnel / HTTPS / public-DNS-override logic was removed —
//     it was tied to a server that no longer answers and added ~150 lines of
//     special-casing we don't need.
//   - Network status is READ from Niklas' net layer (w5500::connected /
//     wifi::connected). We never modify his code; we only read two bools.
//   - Logging via log_info/log_error (this firmware's logging API).
//   - Runs as its own FreeRTOS task (ota_start_task) that polls for a pending
//     action and performs the reboot timer — no Arduino loop() needed.
//   - FUELING-SAFE: an update will not start (and will not pause the pump/probe
//     tasks) while any pump is active. It waits and reports OTA_WAITING_FUELING.
//     It never interrupts a fuelling.
// =======================================================

#include "ota.h"

#include <Ethernet.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "logging.h"

// --- Read-only views into Niklas' network layer (header-only namespaces) ---
namespace w5500 { extern bool connected; }
namespace wifi  { extern bool connected; }

// --- Measurement tasks we pause during a download (we own these APIs) ---
#include "apps/cloudgauge/cloudgauge.h"
#include "apps/pump/pump.h"

namespace {

#ifndef CONTROL_FW_VERSION
#define CONTROL_FW_VERSION "0.1.40"
#endif

const char* kOtaPrefsNs          = "otacfg";
const char* kDefaultOtaBaseUrl   = "http://52.56.195.98/ota/control";
const char* kDefaultOtaManifest  = "http://52.56.195.98/ota/control/manifest.json";
const char* kDefaultOtaReleases  = "http://52.56.195.98/ota/control/releases.json";

const unsigned long kOtaRequestTimeoutMs            = 12000;
const unsigned long kOtaFirmwareInactivityTimeoutMs = 30000;
const size_t        kOtaFirmwareBufferSize          = 512;
const size_t        kOtaFirmwareMaxReadPerLoop      = 512;
const size_t        kOtaProgressLogStep             = 64 * 1024;
const unsigned long kOtaRebootDelayMs               = 4000;
constexpr size_t    kMaxReleaseEntries              = 12;

// How often the OTA task wakes to poll for work / drive the reboot timer.
const TickType_t    kOtaPollInterval = pdMS_TO_TICKS(500);
// Stack for the OTA task. Plain-HTTP download + Update.write is lighter than
// the old TLS path, but flash writes still want headroom.
constexpr uint32_t  kOtaTaskStackBytes = 16384;

enum OtaAction : uint8_t {
    OTA_ACTION_NONE   = 0,
    OTA_ACTION_CHECK  = 1,
    OTA_ACTION_UPDATE = 2
};

struct ParsedUrl {
    String   host;
    uint16_t port;
    String   path;
    bool     valid;
};

struct ManifestData {
    String version;
    String url;
    String sha256;
    size_t size;
    String notes;
    String generatedAt;
    bool   valid;
};

typedef ManifestData ReleaseEntry;   // same fields

OtaState  s_state         = OTA_IDLE;
OtaAction s_pendingAction = OTA_ACTION_NONE;
String    s_statusMessage = "Idle";
String    s_availableVersion;
String    s_selectedVersion;
String    s_selectedTransport = "None";
String    s_selectedUrl;
String    s_manifestSha256;
String    s_manifestUrl = kDefaultOtaManifest;
String    s_releasesUrl = kDefaultOtaReleases;
size_t    s_availableSize     = 0;
size_t    s_downloadedBytes   = 0;
size_t    s_downloadTotalBytes = 0;
bool      s_updateAvailable   = false;
uint32_t  s_statusCounter     = 0;
unsigned long s_lastUpdateMs  = 0;
unsigned long s_lastCheckMs   = 0;
bool      s_rebootPending     = false;
unsigned long s_rebootAtMs    = 0;
ReleaseEntry s_releases[kMaxReleaseEntries];
size_t    s_releaseCount      = 0;
TaskHandle_t s_taskHandle     = nullptr;

// --- Small helpers ---------------------------------------------------------

String jsonEscape(const String& text) {
    String out;
    out.reserve(text.length() + 8);
    for (size_t i = 0; i < text.length(); ++i) {
        const char c = text[i];
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\r' || c == '\n') out += ' ';
        else out += c;
    }
    return out;
}

void setState(OtaState state, const String& message) {
    s_state = state;
    s_statusMessage = message;
    s_lastUpdateMs = millis();
    ++s_statusCounter;
}

const char* stateName(OtaState state) {
    switch (state) {
        case OTA_IDLE:            return "Idle";
        case OTA_CHECKING:        return "Checking";
        case OTA_READY:           return "Ready";
        case OTA_UPDATING:        return "Updating";
        case OTA_SUCCESS:         return "Success";
        case OTA_ERROR:           return "Error";
        case OTA_WAITING_FUELING: return "Waiting (fueling active)";
        default:                  return "Unknown";
    }
}

bool networkUp() {
    return w5500::connected || wifi::connected;
}

bool isNumericLike(const String& value) {
    if (value.isEmpty()) return false;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (!(isdigit((unsigned char)c) || c == '.')) return false;
    }
    return true;
}

String normalizeUrl(const String& url) {
    String out = url;
    out.trim();
    while (out.endsWith("/") && !out.endsWith("://")) out.remove(out.length() - 1);
    return out;
}

String joinUrl(const char* base, const char* path) {
    String out = normalizeUrl(String(base));
    if (path && path[0] != '\0') {
        if (path[0] != '/') out += '/';
        out += path;
    }
    return out;
}

int compareVersion(const String& lhs, const String& rhs) {
    if (lhs == rhs) return 0;
    if (!isNumericLike(lhs) || !isNumericLike(rhs)) return lhs.compareTo(rhs);
    int lStart = 0, rStart = 0;
    while (lStart >= 0 || rStart >= 0) {
        int lDot = lhs.indexOf('.', lStart);
        int rDot = rhs.indexOf('.', rStart);
        const String lPart = lhs.substring(lStart, lDot < 0 ? lhs.length() : lDot);
        const String rPart = rhs.substring(rStart, rDot < 0 ? rhs.length() : rDot);
        const long lVal = lPart.toInt();
        const long rVal = rPart.toInt();
        if (lVal != rVal) return lVal < rVal ? -1 : 1;
        lStart = lDot < 0 ? -1 : lDot + 1;
        rStart = rDot < 0 ? -1 : rDot + 1;
        if (lStart < 0 && rStart < 0) break;
        if (lStart < 0) lStart = lhs.length();
        if (rStart < 0) rStart = rhs.length();
    }
    return 0;
}

ParsedUrl parseUrl(const String& url) {
    ParsedUrl parsed = {"", 0, "/", false};
    String normalized = normalizeUrl(url);
    if (!normalized.startsWith("http://")) return parsed;   // HTTP only
    String rest = normalized.substring(7);
    const int slash = rest.indexOf('/');
    String hostPort = slash >= 0 ? rest.substring(0, slash) : rest;
    parsed.path = slash >= 0 ? rest.substring(slash) : "/";
    const int colon = hostPort.indexOf(':');
    if (colon >= 0) {
        parsed.host = hostPort.substring(0, colon);
        parsed.port = (uint16_t)hostPort.substring(colon + 1).toInt();
    } else {
        parsed.host = hostPort;
        parsed.port = 80;
    }
    parsed.valid = !parsed.host.isEmpty() && parsed.port > 0;
    return parsed;
}

bool validOtaUrl(const String& url) { return parseUrl(url).valid; }

String extractJsonString(const String& json, const char* key) {
    const String needle = String("\"") + key + "\":";
    int pos = json.indexOf(needle);
    if (pos < 0) return String();
    pos += needle.length();
    while (pos < json.length() && isspace((unsigned char)json[pos])) ++pos;
    if (pos >= json.length() || json[pos] != '"') return String();
    ++pos;
    String out;
    while (pos < json.length()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.length()) { out += json[pos++]; continue; }
        if (c == '"') break;
        out += c;
    }
    return out;
}

size_t extractJsonSize(const String& json, const char* key) {
    const String needle = String("\"") + key + "\":";
    int pos = json.indexOf(needle);
    if (pos < 0) return 0;
    pos += needle.length();
    while (pos < json.length() && isspace((unsigned char)json[pos])) ++pos;
    int end = pos;
    while (end < json.length() && isdigit((unsigned char)json[end])) ++end;
    return (size_t)json.substring(pos, end).toInt();
}

ManifestData parseManifest(const String& body) {
    ManifestData m;
    m.version     = extractJsonString(body, "version");
    m.url         = extractJsonString(body, "url");
    m.sha256      = extractJsonString(body, "sha256");
    m.size        = extractJsonSize(body, "size");
    m.notes       = extractJsonString(body, "notes");
    m.generatedAt = extractJsonString(body, "generatedAt");
    m.valid       = !m.version.isEmpty() && !m.url.isEmpty();
    return m;
}

bool extractJsonObject(const String& json, int startPos, String& objectOut, int& endPos) {
    int pos = startPos;
    while (pos < json.length() && json.charAt(pos) != '{') ++pos;
    if (pos >= json.length()) return false;
    int depth = 0; bool inString = false, escaped = false;
    const int begin = pos;
    for (; pos < json.length(); ++pos) {
        const char c = json.charAt(pos);
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) { objectOut = json.substring(begin, pos + 1); endPos = pos + 1; return true; }
        }
    }
    return false;
}

ReleaseEntry parseReleaseEntry(const String& objectJson) {
    return parseManifest(objectJson);   // identical field set
}

size_t parseReleaseList(const String& body, ReleaseEntry* entries, size_t maxEntries) {
    const String key = "\"releases\"";
    int pos = body.indexOf(key);
    if (pos < 0) return 0;
    pos = body.indexOf('[', pos + key.length());
    if (pos < 0) return 0;
    ++pos;
    size_t count = 0;
    while (pos < body.length() && count < maxEntries) {
        String objectJson; int endPos = pos;
        if (!extractJsonObject(body, pos, objectJson, endPos)) break;
        ReleaseEntry entry = parseReleaseEntry(objectJson);
        if (entry.valid) entries[count++] = entry;
        pos = endPos;
    }
    return count;
}

// --- Minimal HTTP (GET) client over EthernetClient / WiFiClient -----------
// Plain HTTP only. Prefers Ethernet when linked, else WiFi.

class HttpClientStream {
public:
    HttpClientStream() : stream_(nullptr) {}
    ~HttpClientStream() { close(); }

    bool open(const String& url, String& transport, String& error,
              int& statusCode, size_t& contentLength, bool& chunked) {
        close();
        ParsedUrl parsed = parseUrl(url);
        if (!parsed.valid) { error = "Only http:// URLs are supported"; return false; }

        const bool hasEth  = w5500::connected;
        const bool hasWifi = wifi::connected;
        if (!hasEth && !hasWifi) { error = "No network transport available"; return false; }

        if (hasEth) {
            eth_.stop();
            if (eth_.connect(parsed.host.c_str(), parsed.port)) {
                stream_ = &eth_;
                transport = "Ethernet HTTP";
                return finishOpen(parsed, transport, error, statusCode, contentLength, chunked);
            }
            error = "Ethernet HTTP connect failed";
        }
        if (hasWifi) {
            wifi_.stop();
            if (wifi_.connect(parsed.host.c_str(), parsed.port)) {
                stream_ = &wifi_;
                transport = "WiFi HTTP";
                return finishOpen(parsed, transport, error, statusCode, contentLength, chunked);
            }
            if (!hasEth) error = "WiFi HTTP connect failed";
        }
        return false;
    }

    int  read(uint8_t* buffer, size_t len) { return stream_ ? stream_->read(buffer, len) : -1; }
    int  available()                       { return stream_ ? stream_->available() : 0; }
    bool connected()                       { return stream_ && stream_->connected(); }

    void close() {
        if (stream_ == &eth_)  eth_.stop();
        else if (stream_ == &wifi_) wifi_.stop();
        stream_ = nullptr;
    }

private:
    bool finishOpen(const ParsedUrl& parsed, const String& /*transport*/, String& error,
                    int& statusCode, size_t& contentLength, bool& chunked) {
        const String request = String("GET ") + parsed.path +
            " HTTP/1.1\r\nHost: " + parsed.host + "\r\nConnection: close\r\n\r\n";
        stream_->print(request);

        String statusLine = readLine(kOtaRequestTimeoutMs);
        if (statusLine.length() == 0) { error = "No HTTP response"; close(); return false; }
        statusLine.trim();
        int firstSpace  = statusLine.indexOf(' ');
        int secondSpace = statusLine.indexOf(' ', firstSpace + 1);
        statusCode = firstSpace >= 0
            ? statusLine.substring(firstSpace + 1, secondSpace < 0 ? statusLine.length() : secondSpace).toInt()
            : 0;

        contentLength = 0; chunked = false;
        while (true) {
            String header = readLine(kOtaRequestTimeoutMs);
            if (header.length() == 0) break;
            header.trim();
            if (header.startsWith("Content-Length:"))
                contentLength = (size_t)header.substring(15).toInt();
            else if (header.startsWith("Transfer-Encoding:") && header.indexOf("chunked") >= 0)
                chunked = true;
        }
        return true;
    }

    String readLine(unsigned long timeoutMs) {
        String line;
        unsigned long start = millis();
        while (millis() - start < timeoutMs) {
            while (stream_ && stream_->available()) {
                char c = (char)stream_->read();
                if (c == '\n') return line;
                if (c != '\r') line += c;
            }
            if (stream_ && !stream_->connected() && !stream_->available()) break;
            delay(1);
        }
        return line;
    }

    EthernetClient eth_;
    WiFiClient     wifi_;
    Client*        stream_;
};

String s_scratchTransport;

bool fetchBody(const String& url, String& bodyOut, String& errorOut) {
    HttpClientStream client;
    String transport; int statusCode = 0; size_t contentLength = 0; bool chunked = false;
    log_info("[ota] fetch: %s", url.c_str());
    if (!client.open(url, transport, errorOut, statusCode, contentLength, chunked)) return false;
    s_scratchTransport = transport;
    s_selectedTransport = transport;
    s_selectedUrl = url;
    if (statusCode != 200) { errorOut = String("HTTP status ") + statusCode; return false; }
    if (chunked) { errorOut = "Chunked responses are not supported"; return false; }

    bodyOut.reserve(contentLength > 0 ? contentLength + 1 : 512);
    uint8_t buffer[256];
    unsigned long lastData = millis();
    while (client.connected() || client.available()) {
        while (client.available()) {
            int n = client.read(buffer, sizeof(buffer));
            if (n > 0) { for (int i = 0; i < n; ++i) bodyOut += (char)buffer[i]; lastData = millis(); }
            else break;
        }
        if ((millis() - lastData) > kOtaRequestTimeoutMs) break;
        delay(1);
    }
    client.close();
    return true;
}

void clearReleaseEntries() {
    s_releaseCount = 0;
    for (size_t i = 0; i < kMaxReleaseEntries; ++i) s_releases[i] = ReleaseEntry();
}

void loadOtaConfigInternal() {
    Preferences prefs;
    prefs.begin(kOtaPrefsNs, true);
    s_manifestUrl = prefs.getString("manifest", kDefaultOtaManifest);
    s_releasesUrl = prefs.getString("releases", kDefaultOtaReleases);
    prefs.end();
    s_manifestUrl = normalizeUrl(s_manifestUrl);
    s_releasesUrl = normalizeUrl(s_releasesUrl);
    if (!validOtaUrl(s_manifestUrl)) s_manifestUrl = joinUrl(kDefaultOtaBaseUrl, "/manifest.json");
    if (!validOtaUrl(s_releasesUrl)) s_releasesUrl = joinUrl(kDefaultOtaBaseUrl, "/releases.json");
}

void storeManifestAsSingleRelease(const ManifestData& m) {
    clearReleaseEntries();
    if (!m.valid) return;
    s_releases[0] = m;
    s_releaseCount = 1;
}

bool refreshReleaseEntries(String& errorOut) {
    clearReleaseEntries();
    String listBody, listError;
    if (fetchBody(s_releasesUrl, listBody, listError)) {
        s_releaseCount = parseReleaseList(listBody, s_releases, kMaxReleaseEntries);
        if (s_releaseCount > 0) {
            log_info("[ota] release list parsed: %u entries", (unsigned)s_releaseCount);
            return true;
        }
        errorOut = "Release list missing entries";
        return false;
    }
    // Fall back to a single-entry manifest if releases.json is unavailable.
    String manifestBody, manifestError;
    log_info("[ota] releases fetch failed, trying manifest: %s", listError.c_str());
    if (!fetchBody(s_manifestUrl, manifestBody, manifestError)) {
        errorOut = listError.isEmpty() ? manifestError : listError;
        return false;
    }
    storeManifestAsSingleRelease(parseManifest(manifestBody));
    if (s_releaseCount == 0) { errorOut = "Manifest missing required fields"; return false; }
    return true;
}

bool findReleaseByVersion(const String& version, ReleaseEntry& out) {
    for (size_t i = 0; i < s_releaseCount; ++i)
        if (s_releases[i].valid && s_releases[i].version == version) { out = s_releases[i]; return true; }
    return false;
}

bool performFirmwareDownload(const ManifestData& manifest, String& errorOut) {
    HttpClientStream client;
    String transport; int statusCode = 0; size_t contentLength = 0; bool chunked = false;
    if (!client.open(manifest.url, transport, errorOut, statusCode, contentLength, chunked)) return false;
    s_selectedTransport = transport;
    s_selectedUrl = manifest.url;

    if (statusCode != 200) { errorOut = String("Firmware HTTP status ") + statusCode; client.close(); return false; }
    if (chunked)           { errorOut = "Chunked firmware downloads not supported"; client.close(); return false; }
    if (manifest.size > 0 && contentLength > 0 && manifest.size != contentLength) {
        errorOut = "Manifest size mismatch"; client.close(); return false;
    }
    const size_t expectedSize = contentLength > 0 ? contentLength : manifest.size;
    const uint32_t freeSketchSpace = ESP.getFreeSketchSpace();
    log_info("[ota] partition space: free=%lu expected=%lu",
             (unsigned long)freeSketchSpace, (unsigned long)expectedSize);
    if (expectedSize > 0 && freeSketchSpace > 0 && expectedSize > freeSketchSpace) {
        errorOut = String("Not enough OTA space: need ") + (unsigned long)expectedSize +
                   ", free " + (unsigned long)freeSketchSpace; client.close(); return false;
    }
    if (!Update.begin(expectedSize > 0 ? expectedSize : UPDATE_SIZE_UNKNOWN)) {
        errorOut = String("Update.begin failed: ") + Update.errorString(); client.close(); return false;
    }

    log_info("[ota] firmware download: %s via %s, expected=%lu bytes",
             manifest.url.c_str(), transport.c_str(), (unsigned long)contentLength);

    uint8_t buffer[kOtaFirmwareBufferSize];
    size_t written = 0;
    s_downloadedBytes = 0;
    s_downloadTotalBytes = contentLength > 0 ? contentLength : manifest.size;
    unsigned long lastData = millis();
    size_t nextProgressLog = kOtaProgressLogStep;
    while (true) {
        if (contentLength > 0 && written >= contentLength) break;
        int available = client.available();
        if (available <= 0) {
            if ((millis() - lastData) > kOtaFirmwareInactivityTimeoutMs) {
                log_info("[ota] download stalled after %lu bytes", (unsigned long)written);
                break;
            }
            delay(5);
            continue;
        }
        const size_t readLimit = sizeof(buffer) < kOtaFirmwareMaxReadPerLoop ? sizeof(buffer) : kOtaFirmwareMaxReadPerLoop;
        int readCount = client.read(buffer, available > (int)readLimit ? readLimit : available);
        if (readCount > 0) {
            size_t w = Update.write(buffer, (size_t)readCount);
            if (w != (size_t)readCount) {
                errorOut = String("Update.write failed: ") + Update.errorString();
                Update.abort(); client.close(); return false;
            }
            written += w;
            s_downloadedBytes = written;
            lastData = millis();
            while (written >= nextProgressLog) {
                log_info("[ota] progress: %lu/%lu bytes", (unsigned long)written, (unsigned long)contentLength);
                nextProgressLog += kOtaProgressLogStep;
            }
            delay(1);
            continue;
        }
        if (!client.connected() && client.available() <= 0)
            log_info("[ota] stream closed after %lu bytes", (unsigned long)written);
        delay(5);
    }
    client.close();

    if (contentLength > 0 && written != contentLength) {
        errorOut = String("Incomplete download: ") + (unsigned long)written + "/" + (unsigned long)contentLength;
        Update.abort(); return false;
    }
    if (!Update.end())      { errorOut = String("Update.end failed: ") + Update.errorString(); return false; }
    if (!Update.isFinished()) { errorOut = "Update did not finish"; return false; }
    s_downloadedBytes = s_downloadTotalBytes;
    return true;
}

void runCheck() {
    if (!networkUp()) { setState(OTA_ERROR, "No network"); return; }
    setState(OTA_CHECKING, "Checking releases...");
    String error;
    if (!refreshReleaseEntries(error) || s_releaseCount == 0) {
        s_updateAvailable = false;
        setState(OTA_ERROR, error);
        log_error("[ota] release check failed: %s", error.c_str());
        return;
    }
    s_lastCheckMs = millis();
    const ReleaseEntry& newest = s_releases[0];
    ReleaseEntry preserved;
    const bool keep = !s_selectedVersion.isEmpty() && findReleaseByVersion(s_selectedVersion, preserved);
    s_availableVersion = newest.version;
    const ReleaseEntry& chosen = keep ? preserved : newest;
    s_availableSize   = chosen.size;
    s_manifestSha256  = chosen.sha256;
    s_selectedVersion = chosen.version;
    s_selectedUrl     = chosen.url;

    if (compareVersion(String(CONTROL_FW_VERSION), newest.version) < 0) {
        s_updateAvailable = true;
        setState(OTA_READY, String("Update available: ") + newest.version);
        log_info("[ota] update available: %s -> %s", CONTROL_FW_VERSION, newest.version.c_str());
    } else {
        s_updateAvailable = false;
        setState(OTA_READY, String("Already up to date: ") + CONTROL_FW_VERSION);
        log_info("[ota] already up to date: %s", CONTROL_FW_VERSION);
    }
}

void runUpdate() {
    if (s_selectedVersion.isEmpty() || s_selectedUrl.isEmpty()) {
        runCheck();
        if (s_selectedVersion.isEmpty() || s_selectedUrl.isEmpty()) return;
    }

    // --- FUELING-SAFE GATE ---------------------------------------------
    // Never interrupt a fuelling. If any pump is active, do not start — flag
    // waiting and let the task retry on the next poll. We re-queue the action.
    if (pump_any_active()) {
        setState(OTA_WAITING_FUELING, "Update deferred: fueling in progress");
        log_info("[ota] update deferred — fueling active");
        s_pendingAction = OTA_ACTION_UPDATE;   // retry next poll
        return;
    }

    ManifestData manifest;
    manifest.version = s_selectedVersion;
    manifest.url     = s_selectedUrl;
    manifest.sha256  = s_manifestSha256;
    manifest.size    = s_availableSize;
    manifest.valid   = !manifest.version.isEmpty() && !manifest.url.isEmpty();

    // Pause measurement tasks so they don't hit the shared network/SPI while
    // we stream the firmware. (We own these APIs; Niklas' net code untouched.)
    log_info("[ota] pausing probe/pump tasks for download");
    cloudgauge_stop();
    pump_stop();

    setState(OTA_UPDATING, String("Downloading ") + manifest.version + "...");
    log_info("[ota] update start: %s via %s", manifest.url.c_str(), s_selectedTransport.c_str());

    String error;
    bool ok = performFirmwareDownload(manifest, error);

    if (!ok) {
        // Download failed — resume the tasks and report. No reboot.
        cloudgauge_start();
        pump_start();
        setState(OTA_ERROR, error);
        log_error("[ota] update failed: %s", error.c_str());
        return;
    }

    s_updateAvailable = false;
    setState(OTA_SUCCESS, String("Update installed: ") + manifest.version + ". Rebooting...");
    log_info("[ota] update installed: %s", manifest.version.c_str());
    // Reboot into the new image. Tasks stay paused — we're about to restart.
    s_rebootPending = true;
    s_rebootAtMs = millis() + kOtaRebootDelayMs;
}

// --- OTA task: poll for a pending action + drive the reboot timer ---------
void otaTaskFn(void* /*param*/) {
    for (;;) {
        if (s_rebootPending && millis() >= s_rebootAtMs) {
            log_info("[ota] rebooting into updated firmware");
            delay(50);
            ESP.restart();
        }

        OtaAction action = s_pendingAction;
        if (action != OTA_ACTION_NONE) {
            s_pendingAction = OTA_ACTION_NONE;
            if (action == OTA_ACTION_CHECK)       runCheck();
            else if (action == OTA_ACTION_UPDATE) runUpdate();
        }

        vTaskDelay(kOtaPollInterval);
    }
}

}  // namespace

// =======================================================
// Public API
// =======================================================

void otaInit() {
    s_state = OTA_IDLE;
    s_pendingAction = OTA_ACTION_NONE;
    s_statusMessage = "Idle";
    s_availableVersion = "";
    s_selectedVersion = "";
    s_selectedTransport = "None";
    s_selectedUrl = "";
    s_manifestSha256 = "";
    s_availableSize = 0;
    s_downloadedBytes = 0;
    s_downloadTotalBytes = 0;
    s_updateAvailable = false;
    s_statusCounter = 1;
    s_lastUpdateMs = millis();
    s_lastCheckMs = 0;
    s_rebootPending = false;
    s_rebootAtMs = 0;
    clearReleaseEntries();
    loadOtaConfigInternal();
}

void ota_start_task() {
    if (s_taskHandle) return;
    xTaskCreate(otaTaskFn, "ota", kOtaTaskStackBytes / sizeof(StackType_t), nullptr, 1, &s_taskHandle);
}

const char* otaCurrentVersion() { return CONTROL_FW_VERSION; }
const char* otaManifestUrl()    { return s_manifestUrl.c_str(); }
const char* otaReleasesUrl()    { return s_releasesUrl.c_str(); }

void otaLoadConfig(String& manifestUrl, String& releasesUrl) {
    loadOtaConfigInternal();
    manifestUrl = s_manifestUrl;
    releasesUrl = s_releasesUrl;
}

bool otaConfigValid(const String& manifestUrl, const String& releasesUrl) {
    return validOtaUrl(manifestUrl) && validOtaUrl(releasesUrl);
}

bool otaSaveConfig(const String& manifestUrl, const String& releasesUrl) {
    if (!otaConfigValid(manifestUrl, releasesUrl)) return false;
    Preferences prefs;
    prefs.begin(kOtaPrefsNs, false);
    const String nm = normalizeUrl(manifestUrl);
    const String nr = normalizeUrl(releasesUrl);
    prefs.putString("manifest", nm);
    prefs.putString("releases", nr);
    prefs.end();
    s_manifestUrl = nm;
    s_releasesUrl = nr;
    clearReleaseEntries();
    return true;
}

// True only while an OTA operation is actively executing (not merely queued).
// A merely-pending action is a sub-poll-interval window and must NOT block a
// new request — that race silently dropped update presses from the UI.
static bool otaRunning() {
    return s_state == OTA_CHECKING
        || s_state == OTA_UPDATING
        || s_state == OTA_WAITING_FUELING;
}

bool otaRequestCheck() {
    if (otaRunning()) return false;    // only block while genuinely running
    s_pendingAction = OTA_ACTION_CHECK;
    return true;
}

bool otaRequestUpdate() {
    if (otaRunning()) return false;    // only block while genuinely running
    s_pendingAction = OTA_ACTION_UPDATE;
    return true;
}

bool otaSelectVersion(const String& version) {
    if (otaRunning() || version.isEmpty()) return false;
    ReleaseEntry entry;
    if (!findReleaseByVersion(version, entry)) {
        String error;
        if (!refreshReleaseEntries(error)) { setState(OTA_ERROR, error); return false; }
        if (!findReleaseByVersion(version, entry)) {
            setState(OTA_ERROR, String("Unknown release: ") + version); return false;
        }
    }
    s_selectedVersion = entry.version;
    s_selectedUrl     = entry.url;
    s_manifestSha256  = entry.sha256;
    s_availableSize   = entry.size;
    s_updateAvailable = compareVersion(String(CONTROL_FW_VERSION), entry.version) < 0;
    setState(OTA_READY, String("Selected release: ") + entry.version);
    return true;
}

bool otaBusy() {
    return s_pendingAction != OTA_ACTION_NONE
        || s_state == OTA_CHECKING
        || s_state == OTA_UPDATING
        || s_state == OTA_WAITING_FUELING;
}

OtaState otaState()               { return s_state; }
String   otaStateText()           { return String(stateName(s_state)); }
String   otaStatusMessage()       { return s_statusMessage; }
String   otaAvailableVersion()    { return s_availableVersion; }
String   otaSelectedVersion()     { return s_selectedVersion; }
String   otaSelectedTransport()   { return s_selectedTransport; }
String   otaSelectedUrl()         { return s_selectedUrl; }
uint32_t otaStatusCounter()       { return s_statusCounter; }
unsigned long otaLastUpdateMs()   { return s_lastUpdateMs; }
unsigned long otaLastCheckMs()    { return s_lastCheckMs; }
bool     otaUpdateAvailable()     { return s_updateAvailable; }
size_t   otaAvailableSize()       { return s_availableSize; }

size_t   otaReleaseCount()        { return s_releaseCount; }
String   otaReleaseVersionAt(size_t index) {
    if (index >= s_releaseCount) return String();
    return s_releases[index].version;
}

size_t   otaDownloadedBytes()     { return s_downloadedBytes; }
size_t   otaDownloadTotalBytes()  { return s_downloadTotalBytes > 0 ? s_downloadTotalBytes : s_availableSize; }

String otaStatusJson() {
    String json = "{";
    json += "\"state\":\"";            json += jsonEscape(otaStateText());
    json += "\",\"busy\":";            json += otaBusy() ? "true" : "false";
    json += ",\"message\":\"";         json += jsonEscape(otaStatusMessage());
    json += "\",\"currentVersion\":\"";json += jsonEscape(String(otaCurrentVersion()));
    json += "\",\"availableVersion\":\"";json += jsonEscape(otaAvailableVersion());
    json += "\",\"updateAvailable\":"; json += otaUpdateAvailable() ? "true" : "false";
    json += ",\"selectedVersion\":\""; json += jsonEscape(otaSelectedVersion());
    json += "\",\"transport\":\"";     json += jsonEscape(otaSelectedTransport());
    json += "\",\"manifestUrl\":\"";   json += jsonEscape(String(otaManifestUrl()));
    json += "\",\"releasesUrl\":\"";   json += jsonEscape(String(otaReleasesUrl()));
    json += "\",\"selectedUrl\":\"";   json += jsonEscape(otaSelectedUrl());
    json += "\",\"size\":";            json += String((unsigned long)otaAvailableSize());
    json += ",\"downloaded\":";        json += String((unsigned long)s_downloadedBytes);
    json += ",\"downloadTotal\":";     json += String((unsigned long)(s_downloadTotalBytes > 0 ? s_downloadTotalBytes : otaAvailableSize()));
    json += ",\"update\":";            json += String(otaStatusCounter());
    json += ",\"millis\":";            json += String(millis());
    json += ",\"lastActionMs\":";      json += String(otaLastUpdateMs());
    json += ",\"lastCheckMs\":";       json += String(otaLastCheckMs());
    json += ",\"releases\":[";
    for (size_t i = 0; i < s_releaseCount; ++i) {
        if (i > 0) json += ",";
        json += "{\"version\":\"";  json += jsonEscape(s_releases[i].version);
        json += "\",\"url\":\"";    json += jsonEscape(s_releases[i].url);
        json += "\",\"size\":";     json += String((unsigned long)s_releases[i].size);
        json += ",\"generatedAt\":\"";json += jsonEscape(s_releases[i].generatedAt);
        json += "\",\"notes\":\"";  json += jsonEscape(s_releases[i].notes);
        json += "\"}";
    }
    json += "]}";
    return json;
}

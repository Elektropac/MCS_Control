#pragma once
// =======================================================
// fueling_auth — authenticated transaction authorization + delivery
// =======================================================
// See transaktion-godkendelse-designnotat.md (2026-11-06).
//
// A fuelling authorization can arrive from the phone app, the Console or the
// server (over BLE / WiFi / Ethernet). It must be AUTHENTIC (not sniffed +
// replayed). This module verifies the authorization and turns it into an
// identified TRANSACTION (txn-id). The rule-engine still owns the relay; a
// successful authorization just sets up the transaction state the engine then
// executes against.
//
// Authentication (transport-independent):
//   device_key = AES-GCM key derived from ( MCS_MASTER_SECRET , device MAC ,
//                key_version ). MAC makes the key device-unique; the shared
//                master secret is what actually keeps it secret (MAC is sent in
//                the clear, so it is identity, not the secret). The same
//                lib/encryption AES-256-GCM used for the BLE session is reused:
//                a command that decrypts + passes the GCM tag with device_key
//                is authentic. A fresh nonce (from auth_challenge) inside the
//                command blocks replay.
//
// ⚠️ MCS_MASTER_SECRET is a PLACEHOLDER until the real value is provisioned.
// =======================================================
#include <Arduino.h>
#include <ArduinoJson.h>

// Call once at startup (after config + MAC are available).
void fueling_auth_init();

// --- function_silo entry points (called from the dispatch) ---

// auth_challenge: issue a fresh one-time nonce the caller must echo back in the
// next fuel_authorize. Returns {"nonce":"..."}.
String fueling_auth_challenge();

// fuel_authorize: verify the authenticated request, run the 4 NO-checks, and on
// success create a transaction. `data` is the already-decrypted outer silo
// payload; it must contain an encrypted "auth" blob (hex) produced with
// device_key, which wraps {pump, preset?, nonce, key_ver}.
// Returns {"ok":true,"txn_id":N,"pump":"..."} or {"error":...,"code":N,"text":...}.
String fueling_auth_authorize(JsonObject data);

// fuel_cancel: cancel/free a transaction that has not started fuelling.
String fueling_auth_cancel(JsonObject data);

// txn_pending: list transactions not yet acknowledged by the server.
String fueling_auth_pending();

// txn_ack: mark a transaction acknowledged (OK returned from the server, via
// direct online delivery or carried back by the app).
String fueling_auth_ack(JsonObject data);

// True if the unacked-transaction queue is full. The authorize path refuses new
// fuellings when this is true (measurement-critical: never drop a transaction).
bool fueling_auth_queue_full();

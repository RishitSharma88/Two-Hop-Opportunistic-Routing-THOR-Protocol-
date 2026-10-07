/*
 * THOR smoke validation (SIMULATION ONLY).
 * Matches CURRENT THOR.h public API. THOR.h/.cpp, Roles.h,
 * State_Machine.h are untouched.
 *
 * Validates:
 *  - TempId LSB encoding, Serialize/Deserialize roundtrip
 *  - HELLO -> HandleHello -> ACK -> HandleAck handshake
 *  - SendPacket queues when no route, ProcessQueue stays empty
 *  - PushDestId/GetDestID, RemoveOld, GetBestNextHop don't crash
 *  - Stub per-state wait-loops (future JNI/HW confirm pattern):
 *    sequential false-checks, true only at end.
 */
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include "THOR.h"

static void Step(const std::string& name) {
    std::cout << "\n========== " << name << " ==========\n";
}
static void PrintBytes(const std::vector<uint8_t>& d) {
    std::cout << "[ ";
    for (auto b : d) printf("%02X ", b);
    std::cout << "] (" << d.size() << " bytes)\n";
}
static int gPass = 0, gFail = 0;
static void Check(bool ok, const std::string& label) {
    if (ok) { gPass++; std::cout << "[PASS] " << label << "\n"; }
    else    { gFail++; std::cout << "[FAIL] " << label << "\n"; }
}

// ---- STUB: future Android/JNI confirm pattern (per-state wait loop) ----
// Each state function will own one of these: poll a flag set by the
// JNI/HW callback, timeout -> return false (stage error), else keep moving.
// No outer state-machine loop needed; Run_machine() just false-checks each.
static bool StubWaitForFlag(std::atomic<bool>& flag, int timeoutMs,
                            const std::string& stage) {
    auto t0 = std::chrono::steady_clock::now();
    while (!flag.load()) {
        auto dt = std::chrono::steady_clock::now() - t0;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() > timeoutMs) {
            std::cout << "[STUB] " << stage << " timeout -> stage error\n";
            return false; // caller maps to ErrorCode::XxxTimeout, throws for this stage only
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// Stub JNI callback: pretends hardware confirmed broadcast.
static void StubJniBroadcastConfirm(std::atomic<bool>& flag, int delayMs) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    flag.store(true);
}

static void SetupNode(THOR& n, uint32_t permId, bool internet) {
    n.cfg.PermId = permId & ~1u; // wrapper guarantee: LSB 0
    n.cfg.myInternet = internet;
    n.cfg.deviceType = 0; // Android
    n.cfg.attMtu = 0;     // let InitConfig clamp
    n.cfg.maxPayload = 0;
    n.cfg.fragPayloadSize = 0;
    n.cfg.maxFragments = 0;
    n.InitConfig();
}

int main() {
    Step("0: TempId LSB encoding");
    THOR nodeA, nodeB, nodeC;
    SetupNode(nodeA, 100, false);
    SetupNode(nodeB, 200, false);
    SetupNode(nodeC, 300, true);
    Check((nodeA.cfg.TempId & 1u) == 0, "A no-internet -> LSB 0");
    Check((nodeC.cfg.TempId & 1u) == 1, "C internet -> LSB 1");
    Check(nodeA.ParseTempId() == (100u & ~1u), "ParseTempId masks LSB");
    Check(nodeA.cfg.maxPayload > 0, "MTU clamp computed maxPayload");

    Step("1: Serialize / Deserialize roundtrip");
    Packet p;
    p.header.type = THORPacketType::DATA;
    p.header.flagsAndTTL.setTTL(15);
    p.header.destinationId = 9999;
    p.header.senderId = nodeA.cfg.TempId;
    p.header.originId = nodeA.cfg.TempId;
    p.header.nextHopId = 0;
    p.header.sequence = 1;
    p.payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    auto bytes = nodeA.Serialize(p);
    Packet out;
    Check(nodeA.Deserialize(bytes, out), "Deserialize ok");
    Check(out.header.destinationId == 9999 && out.payload == p.payload, "roundtrip intact");
    Header hout;
    Check(nodeA.DeserializeHeader(bytes, hout), "DeserializeHeader ok");
    std::vector<uint8_t> tiny = {0x01};
    Check(!nodeA.Deserialize(tiny, out), "short buffer rejected");

    Step("2: HELLO -> HandleHello -> ACK -> HandleAck (current API)");
    auto helloB = nodeB.CreateHello(); // NOTE: 0 args in new arch
    Check(!helloB.empty(), "CreateHello non-empty");
    PrintBytes(helloB);
    bool h1 = nodeA.HandleHello(helloB);
    Check(h1, "A HandleHello(B) accepted (transaction opened)");
    // Second hello while transaction open must be rejected by design
    auto helloC = nodeC.CreateHello();
    Check(!nodeA.HandleHello(helloC), "2nd HandleHello rejected while busy (expected)");
    auto ackFromA = nodeA.ACK(); // A replies to B's hello
    Check(!ackFromA.empty(), "ACK() produced packet");
    PrintBytes(ackFromA);
    Check(nodeB.HandleAck(ackFromA), "B HandleAck ok (no crash)");

    Step("3: SendPacket queues with no usable route");
    std::string msg = "Help Me";
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    auto direct = nodeA.SendPacket(9999, nodeA.cfg.TempId, nodeA.cfg.TempId, 1, payload);
    // nodeA's only neighbor is locked (HandleHello sets lock=true), so expect queued
    Check(direct.empty(), "SendPacket queued (empty = stored)");
    auto batch = nodeA.ProcessQueue();
    Check(batch.empty(), "ProcessQueue empty while route locked (expected)");
    uint32_t hop = nodeA.GetBestNextHop();
    std::cout << "BestNextHop=" << hop << " (0 = none/locked, expected for now)\n";

    Step("4: DestId FIFO + RemoveOld smoke");
    Check(nodeA.PushDestId(777), "PushDestId ok");
    auto arr = nodeA.GetDestID();
    Check(arr[0] == 777, "GetDestID returns pushed id");
    nodeA.RemoveOld();
    Check(true, "RemoveOld no crash");

    Step("5: Stub per-state wait-loop (future JNI pattern)");
    std::atomic<bool> flagOk{false};
    std::thread jni(StubJniBroadcastConfirm, std::ref(flagOk), 50);
    Check(StubWaitForFlag(flagOk, 2000, "InitHello") == true, "wait-loop success path");
    jni.join();
    std::atomic<bool> flagNever{false};
    Check(StubWaitForFlag(flagNever, 100, "AckReceive") == false, "wait-loop timeout -> stage error");
    // Sequential false-check chain, true only at end (user's design):
    bool stage1 = true;   // InitHello confirmed above
    bool stage2 = false;  // AckReceive timed out above
    bool runOk = false;
    if (!stage1) { std::cout << "throw HelloTimeout\n"; }
    else if (!stage2) { std::cout << "throw AckTimeout (stage-only error)\n"; }
    else { runOk = true; }
    Check(!runOk, "chain stays false until all stages pass (true only at end)");

    Step("FINAL");
    std::cout << "PASS=" << gPass << " FAIL=" << gFail << "\n";
    std::cout << (gFail == 0 ? "THOR smoke OK for now.\n" : "THOR smoke has failures.\n");
    return gFail == 0 ? 0 : 1;
}

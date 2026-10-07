#pragma once
#include "THOR.h"

enum class Roles : uint8_t { Initiator = 1, Acceptor = 2 };
enum class States : uint8_t 
{
  InitHello = 1,       // Initiator
  DiscoverHello = 2,   // Acceptor
  AckReceive = 3,      // Initiator & Acceptor
  InitiateSession = 4, // Initiator
  WaitForConfirm = 5, // Initiator & Acceptor - Includes Waiting For Heartbeat Check
  Disconnecting = 6, // Initiator & Acceptor - Send Final Acknowledgement, Unlock the path.
  Disconnected = 7 // Initiator & Acceptor - Resetting the states, Shift to next state according to the next operation.
};

// Initiator FLowchart -> InitHello -> AckReceive -> InitiateSession -> WaitForConfirm -> Disconnecting -> Disconnected 
// Acceptor Flowchart -> DiscoverHello -> AckReceive -> WaitForConfirm -> Disconnecting -> Disconnected

class State_Role 
{
public:
  uint32_t seq;
  void caller(uint32_t sequence) 
  { seq = sequence; }
  
  States transition(States state, Roles role) 
  {
    if (role == Roles::Initiator) 
    {
      switch (state) 
      {
        case States::InitHello:
          if (CheckInitHello()) return States::AckReceive;
          break;
        case States::AckReceive:
          if (CheckAckReceive()) return States::InitiateSession;
          break;
        case States::InitiateSession:
          if (CheckInitiateSession()) return States::WaitForConfirm;
          break;
        case States::WaitForConfirm:
          if (CheckWaitForConfirm()) return States::Disconnecting;
          break;
        case States::Disconnecting:
          if (CheckDisconnecting()) return States::Disconnected;
          break;
        case States::Disconnected:
          // Shift to next state according to the next operation
          break;
      }
    } 
    else if (role == Roles::Acceptor) 
    {
      switch (state) 
      {
        case States::DiscoverHello:
          if (CheckDiscoverHello()) return States::AckReceive;
          break;
        case States::AckReceive:
          if (CheckAckReceive()) return States::WaitForConfirm;
          break;
        case States::WaitForConfirm:
          if (CheckWaitForConfirm()) return States::Disconnecting;
          break;
        case States::Disconnecting:
          if (CheckDisconnecting()) return States::Disconnected;
          break;
        case States::Disconnected:
          break;
      }
    }
    return state;
  }

  
bool CheckInitHello(bool res) {
    if (res) { return true; } else { return false; }
}

bool CheckDiscoverHello(bool res) {
    if (res) { return true; } else { return false; }
}

bool CheckAckReceive(bool res) {
    if (res) { return true; } else { return false; }
}

bool CheckInitiateSession(bool res) {
    if (res) { return true; } else { return false; }
}

bool CheckWaitForConfirm(bool res) {
    if (res) { return true; } else { return false; }
}

bool CheckDisconnecting(bool res) {
    if (res) { return true; } else { return false; }
}

bool result;

// check for the best Ack and transition to next phase within 20 seconds
result = CheckInitHello(someBooleanValue); 

// Send the Hello packets with sequence
result = CheckDiscoverHello(someBooleanValue); 

result = CheckAckReceive(someBooleanValue);
result = CheckInitiateSession(someBooleanValue);
result = CheckWaitForConfirm(someBooleanValue);
result = CheckDisconnecting(someBooleanValue);

};

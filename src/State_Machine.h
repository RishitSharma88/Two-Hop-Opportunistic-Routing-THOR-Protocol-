#pragma once

#include "THOR.h"
#include "Roles.h"

#define DEST_ARRAY_SIZE 10
namespace StateMachine
{
    class State_machine
    {
    public:
        std::array<int32_t, DEST_ARRAY_SIZE> destArray{};
        THOR thor{};

        // Both check objects initialized
        Initiator_Checks initiatorChecks{};
        Acceptor_Checks acceptorChecks{};

        void InitMachine()
        {
            thor.InitConfig();
        }

        bool Run_machine()
        {
            InitMachine();
            if(thor.isInitiator)
            {
                if(InitHelloStage())
                {
                    return true;
                }
                return false;
            }
        }

        bool InitHelloStage()
        {
            if (thor.isInitiator)
            {
                return initiatorChecks.InitHello;
            }
            else
            {
                return acceptorChecks.DiscoverHello;
            }
        }
    };
}
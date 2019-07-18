/**
 * @file   crossbar_scheduler.h
 * @date   01/2019
 * @author Alejandro Morais
 * @brief  Resource-constraint scheduler and code generator for crossbar
 */

#ifndef QL_CROSSBAR_SCHEDULER_H
#define QL_CROSSBAR_SCHEDULER_H

#include <ql/utils.h>
#include <ql/gate.h>
#include <ql/ir.h>
#include <ql/circuit.h>
#include <ql/scheduler.h>
#include <ql/arch/crossbar/crossbar_resource_manager.h>

namespace ql
{
namespace arch
{
namespace crossbar
{

class crossbar_scheduler {
public:
    
    /**
     * Normal schedule ASAP or ALAP.
     */
    static ql::ir::bundles_t schedule(ql::circuit& ckt,
        const ql::quantum_platform& platform, size_t num_qubits, size_t num_creg = 0)
    {
        IOUT("Scheduling Crossbar instructions...");

        Scheduler scheduler;
        scheduler.Init(ckt, platform, num_qubits, num_creg);
        ql::ir::bundles_t bundles;
        std::string scheduler_opt = ql::options::get("scheduler");
        
        if (scheduler_opt == "ASAP")
        {
            bundles = scheduler.schedule_asap();
        }
        else if (scheduler_opt == "ALAP")
        {
            bundles = scheduler.schedule_alap();
        }
        else
        {
            EOUT("Unknown scheduler");
            throw ql::exception("Unknown scheduler", false);
        }

        IOUT("Scheduling Crossbar instruction done");
        return bundles;
    }

    /**
     * Schedule ASAP or ALAP based on the availability of resources.
     */
    static ql::ir::bundles_t schedule_rc(ql::circuit& ckt, const ql::quantum_platform& platform,
        crossbar_state_t* initial_crossbar_state, crossbar_state_t* final_crossbar_state,
        size_t num_qubits, size_t num_creg = 0)
    {
        IOUT("Resource-constraint scheduling of Crossbar instructions ...");

        Scheduler scheduler;
        scheduler.Init(ckt, platform, num_qubits, num_creg);
        ql::ir::bundles_t bundles;
        std::string scheduler_opt = ql::options::get("scheduler");
        
        if (scheduler_opt == "ASAP")
        {
            crossbar_resource_manager_t rm(platform, forward_scheduling, 0,
                initial_crossbar_state, final_crossbar_state);
            bundles = scheduler.schedule_asap(rm, platform);
        }
        else if (scheduler_opt == "ALAP")
        {
            crossbar_resource_manager_t rm(platform, backward_scheduling, ALAP_SINK_CYCLE,
                initial_crossbar_state, final_crossbar_state);
            bundles = scheduler.schedule_alap(rm, platform);
        }
        else
        {
            EOUT("Unknown scheduler");
            throw ql::exception("Unknown scheduler", false);
        }

        IOUT("Resource-constraint scheduling of Crossbar instructions done");
        return bundles;
    }
};

}
}
}

#endif // QL_CROSSBAR_SCHEDULER_H

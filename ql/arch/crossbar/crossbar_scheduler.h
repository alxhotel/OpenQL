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

private:
    
    /**
    * Combine parallel instructions of same type from different sections
    * into a single section
    */
    static ql::ir::bundles_t combine_parallel_instructions(const ql::quantum_platform& platform, ql::ir::bundles_t bundles)
    {
        for (ql::ir::bundle_t& a_bundle : bundles)
        {
            auto secIt1 = a_bundle.parallel_sections.begin();
            auto firstInsIt = secIt1->begin();
            auto itype = (*(firstInsIt))->type();
            if (__classical_gate__ == itype)
            {
                continue;
            }
            else
            {
                for ( ; secIt1 != a_bundle.parallel_sections.end(); ++secIt1 )
                {
                    for (auto secIt2 = std::next(secIt1); secIt2 != a_bundle.parallel_sections.end(); ++secIt2)
                    {
                        auto insIt1 = secIt1->begin();
                        auto insIt2 = secIt2->begin();
                        if (insIt1 != secIt1->end() && insIt2 != secIt2->end())
                        {
                            auto id1 = (*insIt1)->name;
                            auto id2 = (*insIt2)->name;
                            auto n1 = platform.get_instruction_name(id1);
                            auto n2 = platform.get_instruction_name(id2);
                            if (n1 == n2)
                            {
                                DOUT("Splicing " << n1 << " and " << n2);
                                (*secIt1).splice(insIt1, (*secIt2));
                            }
                            else
                            {
                                DOUT("Not splicing " << n1 << " and " << n2);
                            }
                        }
                    }
                }
            }
        }

        IOUT("Removing empty sections...");
        ql::ir::bundles_t new_bundles;
        for (ql::ir::bundle_t& a_bundle : bundles)
        {
            ql::ir::bundle_t new_bundle;
            new_bundle.start_cycle = a_bundle.start_cycle;
            new_bundle.duration_in_cycles = a_bundle.duration_in_cycles;
            for (auto& sec : a_bundle.parallel_sections)
            {
                if (!sec.empty())
                {
                     new_bundle.parallel_sections.push_back(sec);
                }
            }
            new_bundles.push_back(new_bundle);
        }
        IOUT("Resource-constraint scheduling of Crossbar instructions done.");
        return new_bundles;
    }
};

}
}
}

#endif // QL_CROSSBAR_SCHEDULER_H

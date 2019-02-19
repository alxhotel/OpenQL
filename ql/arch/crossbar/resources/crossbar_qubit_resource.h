/**
 * @file   crossbar_qubit_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Qubit resource
 */

#ifndef QL_CROSSBAR_QUBIT_RESOURCE_H
#define QL_CROSSBAR_QUBIT_RESOURCE_H

#include <vector>
#include <ql/resource_manager.h>
#include <ql/arch/crossbar/crossbar_state.h>
#include <ql/arch/crossbar/crossbar_resource_manager.h>

namespace ql
{
namespace arch
{
namespace crossbar
{

/**
 * Qubit resource type
 */
class crossbar_qubit_resource_t : public resource_t
{
public:
    /**
     * fwd: qubit q is busy till cycle=state[q], i.e. all cycles < state[q] it is busy, i.e. start_cycle must be >= state[q]
     * bwd: qubit q is busy from cycle=state[q], i.e. all cycles >= state[q] it is busy, i.e. start_cycle+duration must be <= state[q]
     */
    std::vector<size_t> state;
    
    crossbar_qubit_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir) : resource_t("qubits", dir)
    {
        count = platform.qubit_number;
        state.reserve(count);
        for (size_t i = 0; i < count; i++)
        {
            state[i] = (dir == forward_scheduling ? 0 : MAX_CYCLE);
        }
    }

    crossbar_qubit_resource_t* clone() const & { return new crossbar_qubit_resource_t(*this);}
    crossbar_qubit_resource_t* clone() && { return new crossbar_qubit_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        for (auto q : ins->operands)
        {
            if (direction == forward_scheduling)
            {
                DOUT(" available " << name << "? op_start_cycle: " << op_start_cycle << "  qubit: " << q << " is busy till cycle : " << state[q]);
                if (state[q] > op_start_cycle)
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else
            {
                DOUT(" available " << name << "? op_start_cycle: " << op_start_cycle << "  qubit: " << q << " is busy from cycle : " << state[q]);
                if (state[q] < op_start_cycle + operation_duration)
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
        }
        DOUT("    " << name << " resource available ...");
        return true;
    }

    void reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        for (auto q : ins->operands)
        {
            state[q] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            DOUT("reserved " << name << ". op_start_cycle: " << op_start_cycle << " qubit: " << q << " reserved till/from cycle: " << state[q]);
        }
    }
    ~crossbar_qubit_resource_t() {}
};

}
}
}

#endif // QL_CROSSBAR_QUBIT_RESOURCE_H


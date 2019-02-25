/**
 * @file   crossbar_wave_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Wave resource
 */

#ifndef CROSSBAR_WAVE_RESOURCE_H
#define CROSSBAR_WAVE_RESOURCE_H

#include <string>
#include <ql/resource_manager.h>
#include <ql/arch/crossbar/crossbar_state.h>

namespace ql
{
namespace arch
{
namespace crossbar
{
    
class crossbar_wave_resource_t : public resource_t
{
public:
    size_t wave_busy;
    std::string gate;
    
    crossbar_wave_resource_t(const ql::quantum_platform & platform, ql::scheduling_direction_t dir) : resource_t("wave", dir)
    {
        wave_busy = (forward_scheduling == dir ? 0 : MAX_CYCLE);
        gate = "";
    }
    
    crossbar_wave_resource_t* clone() const & { return new crossbar_wave_resource_t(*this);}
    crossbar_wave_resource_t* clone() && { return new crossbar_wave_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        if (instruction_type.compare("single_qubit_gate") == 0)
        {
            // Single qubit gate
            if (!check_wave(op_start_cycle, operation_duration))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
        }
        
        DOUT("    " << name << " resource available ...");
        return true;
    }
    
    void reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        if (instruction_type.compare("single_qubit_gate") == 0)
        {
            // Single qubit gate
            
            if (!operation_name.compare("z_shuttle_left") == 0
                || !operation_name.compare("z_shuttle_right") == 0)
            {
                wave_busy = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;   
            }
        }
    }

private:
    bool check_wave(size_t op_start_cycle, size_t operation_duration)
    {
        if (direction == forward_scheduling)
        {
            if (op_start_cycle < wave_busy)
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
        }
        else
        {
            if (op_start_cycle + operation_duration > wave_busy)
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
        }
        
        return true;
    }
};

}
}
}

#endif // CROSSBAR_WAVE_RESOURCE_H

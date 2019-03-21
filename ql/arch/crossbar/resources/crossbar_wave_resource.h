/**
 * @file   crossbar_wave_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Wave resource
 */

#ifndef CROSSBAR_WAVE_RESOURCE_H
#define CROSSBAR_WAVE_RESOURCE_H

#include <map>
#include <string>
#include <ql/arch/crossbar/crossbar_resource.h>
#include <ql/arch/crossbar/crossbar_state.h>

#include "interval_tree.h"

namespace ql
{
namespace arch
{
namespace crossbar
{

class crossbar_wave_resource_t : public crossbar_resource_t
{
public:
    static int WAVE_DURATION_CYCLES;
    
    Intervals::IntervalTree<size_t, std::string> wave;
    
    crossbar_wave_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, std::map<size_t, crossbar_state_t*> crossbar_states_local)
        : crossbar_resource_t("wave", dir)
    {
        //AWVE_DURATION_CYCLES = (int) platform.resources["wave"]["wave_duration"] / (int) platform.hardware_settings["cycle_time"];
    }
    
    crossbar_wave_resource_t* clone() const & { return new crossbar_wave_resource_t(*this);}
    crossbar_wave_resource_t* clone() && { return new crossbar_wave_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        if (instruction_type.compare("single_qubit_gate") == 0)
        {
            // Single qubit gate
            if (operation_name.rfind("_shuttle") == std::string::npos)
            {
                if (!check_wave(op_start_cycle, WAVE_DURATION_CYCLES, operation_name))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                if (!check_wave(op_start_cycle + operation_duration - WAVE_DURATION_CYCLES, WAVE_DURATION_CYCLES, operation_name))
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
        if (instruction_type.compare("single_qubit_gate") == 0)
        {
            // Single qubit gate
            if (operation_name.rfind("_shuttle") == std::string::npos)
            {
                reserve_wave(op_start_cycle, WAVE_DURATION_CYCLES, operation_name);
                reserve_wave(op_start_cycle + operation_duration - WAVE_DURATION_CYCLES,
                    WAVE_DURATION_CYCLES, operation_name);
            }
        }
    }

private:
    bool check_wave(size_t op_start_cycle, size_t operation_duration, std::string operation_name)
    {
        if (direction == forward_scheduling)
        {
            const auto &intervals = wave.findOverlappingIntervals(
                {op_start_cycle, op_start_cycle + operation_duration}
            );
            
            for (const auto &interval : intervals)
            {
                if (interval.value.compare(operation_name) != 0)
                {
                    return false;
                }
            }
        }
        else
        {
            // TODO
        }
        
        return true;
    }
    
    void reserve_wave(size_t op_start_cycle, size_t operation_duration, std::string operation_name)
    {
        wave.insert({op_start_cycle, op_start_cycle + operation_duration, operation_name});
    }
};

int crossbar_wave_resource_t::WAVE_DURATION_CYCLES = 0;

}
}
}

#endif // CROSSBAR_WAVE_RESOURCE_H

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
#include <ql/arch/crossbar/crossbar_state_map.h>

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
    static int SHUTTLE_DURATION_CYCLE;
    
    Intervals::IntervalTree<size_t, std::string> wave;
    
    crossbar_wave_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, crossbar_state_map_t* crossbar_state_map_local)
        : crossbar_resource_t("wave", dir)
    {
        WAVE_DURATION_CYCLES = (int) platform.resources["wave"]["wave_duration"] / (int) platform.hardware_settings["cycle_time"];
        SHUTTLE_DURATION_CYCLE = (int) platform.instruction_settings["shuttle_up"]["duration"] / (int) platform.hardware_settings["cycle_time"];
    }
   
    crossbar_wave_resource_t* clone() const & { return new crossbar_wave_resource_t(*this);}
    crossbar_wave_resource_t* clone() && { return new crossbar_wave_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        if (available_or_reserve(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration, false))
        {
            DOUT("    " << name << " resource available ...");
            return true;
        }
        
        return false;
    }
    
    void reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        available_or_reserve(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration, true);
    }

private:
    bool check_wave(size_t op_start_cycle, size_t operation_duration, std::string operation_name)
    {
        std::cout << "Check wave[" << operation_name << "]"
                << " from " << op_start_cycle << " to " << op_start_cycle + operation_duration
                << std::endl << std::flush;
        
        const auto &intervals = wave.findOverlappingIntervals(
            {op_start_cycle, op_start_cycle + operation_duration},
            false
        );

        // NOTE: Does not matter the direction of the scheduling
        for (const auto &interval : intervals)
        {
            if (interval.value.compare(operation_name) != 0)
            {
                return false;
            }
        }
        
        return true;
    }
    
    void reserve_wave(size_t op_start_cycle, size_t operation_duration, std::string operation_name)
    {
        std::cout << "Reserve wave[" << operation_name << "]"
                << " from " << op_start_cycle << " to " << op_start_cycle + operation_duration
                << std::endl << std::flush;
        
        wave.insert({op_start_cycle, op_start_cycle + operation_duration, operation_name});
    }
    
    bool available_or_reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration,
        bool reserve)
    {
        if (instruction_type.compare("single_qubit_gate") == 0)
        {
            // Single qubit gate
            if (operation_name.rfind("_shuttle") == std::string::npos)
            {
                // First wave
                if (!check_wave(op_start_cycle, WAVE_DURATION_CYCLES, operation_name))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                // Second wave
                if (!check_wave(op_start_cycle + WAVE_DURATION_CYCLES + SHUTTLE_DURATION_CYCLE, WAVE_DURATION_CYCLES, operation_name))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }

                // RESERVE
                if (reserve)
                {
                    // First wave
                    reserve_wave(op_start_cycle, WAVE_DURATION_CYCLES, operation_name);

                    // Second wave
                    reserve_wave(op_start_cycle + WAVE_DURATION_CYCLES + SHUTTLE_DURATION_CYCLE,
                        WAVE_DURATION_CYCLES, operation_name);
                }
            }
        }
        
        return true;
    }
};

int crossbar_wave_resource_t::WAVE_DURATION_CYCLES = 0;
int crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE = 0;

}
}
}

#endif // CROSSBAR_WAVE_RESOURCE_H

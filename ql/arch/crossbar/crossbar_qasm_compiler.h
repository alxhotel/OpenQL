/**
 * @file   crossbar_cqasm_compiler.h
 * @date   01/2019
 * @author Alejandro Morais
 * @brief  Crossbar cqasm compiler implementation
 */

#ifndef QL_CROSSBAR_QASM_COMPILER_H
#define QL_CROSSBAR_QASM_COMPILER_H

#include <ql/utils.h>
#include <ql/platform.h>
#include <ql/kernel.h>
#include <ql/gate.h>
#include <ql/ir.h>
#include <ql/eqasm_compiler.h>
#include <ql/arch/crossbar/crossbar_scheduler.h>

namespace ql
{
namespace arch
{
namespace crossbar
{

/**
 * Crossbar qasm compiler
 */
class crossbar_qasm_compiler : public eqasm_compiler
{
public:
    
    size_t num_qubits;
    size_t ns_per_cycle;

public:
    
    /**
     * Execute the mapping process
     */
    void map(std::string prog_name, std::vector<quantum_kernel>& kernels, const ql::quantum_platform& platform)
    {
        /*for(auto &kernel : kernels)
        {
            // don't trust the cycle fields in the instructions
            // and let write_qasm print the circuit instead of the bundles
            kernel.bundles.clear();
        }

        Mapper mapper;  // virgin mapper creation; for role of Init functions, see comment at top of mapper.h
        mapper.Init(platform); // platform specifies number of real qubits, i.e. locations for virtual qubits
        for(auto &kernel : kernels)
        {
            auto mapopt = ql::options::get("mapper");
            if (mapopt == "no" )
            {
                IOUT("Not mapping kernel");
                continue;;
            }
            IOUT("Mapping kernel: " << kernel.name);
            mapper.MapCircuit(kernel.c, kernel.name, kernel.qubit_count, kernel.creg_count);
                // kernel.qubit_count is number of virtual qubits, i.e. highest indexed qubit minus 1
                // and kernel.qubit_count is updated to real highest index used minus -1
            kernel.bundles = mapper.Bundler(kernel.c);
            mapper.GetNumberOfSwapsAdded(kernel.swaps_added);
            mapper.GetNumberOfMovesAdded(kernel.moves_added);
        }
        std::stringstream mapper_out_fname;
        mapper_out_fname << ql::options::get("output_dir") << "/" << prog_name << "_mapper_out.qasm";
        IOUT("writing mapper output qasm to '" << mapper_out_fname.str() << "' ...");
        write_qasm(mapper_out_fname, kernels, platform);*/
    }
    
    /**
     * Program-level compilation of qasm to crossbar_qasm
     */
    void compile(std::string prog_name, ql::circuit& ckt, ql::quantum_platform& platform)
    {
        EOUT("This compile method is not supported for the Crossbar platform");
    }

    /**
     * Compile a list of kernels
     */
    void compile(std::string prog_name, std::vector<quantum_kernel> kernels, const ql::quantum_platform& platform)
    {
        DOUT("Compiling " << kernels.size() << " kernels to generate Crossbar cQASM ... ");

        // Crossbar HW params
        load_hw_settings(platform);

        // TOOD: Do mapping
        
        // map(prog_name, kernels, platform);
        
        // Translate qubits to sites
        kernels = qubits_to_sites(kernels, platform);
        
        // TOOD: Do routing
        
        std::stringstream qasm_ins_str;
        for (auto &kernel : kernels)
        {
            IOUT("Compiling kernel: " << kernel.name);
            ql::circuit& ckt = kernel.c;
            
            if (!ckt.empty())
            {
                // Schedule with platform resource constraints
                ql::ir::bundles_t bundles = crossbar_scheduler::schedule_rc(ckt, platform, num_qubits);

                // Translate sites back to qubits
                //bundles = sites_to_qubits(bundles, platform);
                
                // TODO: Decompose instructions to "native instructions"
                
                // Convert to string
                qasm_ins_str << "" + ql::ir::qasm(bundles) + "\n";
            }
        }

        std::string file_name = "" + prog_name + "_compiled";
        IOUT("Writing Crossbar cQASM compiled to " + file_name);
        write_to_file(file_name, qasm_ins_str.str());

        DOUT("Compilation of Crossbar cQASM done");
    }

private:
    
    /**
     * Convert instructions operands from qubits to sites
     * 
     * Note: this also adds a second site for the dependency graph
     */
    std::vector<quantum_kernel> qubits_to_sites(std::vector<quantum_kernel> kernels, const ql::quantum_platform& platform)
    {
        IOUT("Translating qubits to sites");
        
        // Init crossbar state with initial placement
        crossbar_state_t* crossbar_state = this->get_init_crossbar_state(platform);
        int n = crossbar_state->get_x_size();
        
        for (auto &kernel : kernels)
        {
            ql::circuit& ckt = kernel.c;
            for (auto &ins : ckt)
            {
                std::string op_name = ins->name;
                std::vector<size_t> & operands = ins->operands;
                
                size_t qubit_index = operands[0];
                
                // Translate qubits ti sites
                for (size_t key = 0; key < operands.size(); key++)
                {
                    operands[key] = crossbar_state->get_site_by_qubit(operands[key]);
                }
                
                std::vector<size_t> & sites = operands;
                
                if (op_name.rfind("shuttle", 0) == 0)
                {
                    // Shuttle
                    
                    if (op_name.compare("shuttle_up") == 0)
                    {
                        sites.push_back(sites[0] + n);
                        crossbar_state->shuttle_up(qubit_index);
                    }
                    else if (op_name.compare("shuttle_down") == 0)
                    {
                        sites.push_back(sites[0] - n);
                        crossbar_state->shuttle_down(qubit_index);
                    }
                    else if (op_name.compare("shuttle_left") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                        crossbar_state->shuttle_left(qubit_index);
                    }
                    else if (op_name.compare("shuttle_right") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                        crossbar_state->shuttle_right(qubit_index);
                    }
                }
                else if (op_name.rfind("z_shuttle", 0) == 0
                        || op_name.rfind("s_shuttle", 0) == 0
                        || op_name.rfind("t_shuttle", 0) == 0)
                {
                    // Z, S & T Gate
                    
                    if (op_name.rfind("shuttle_left") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                    }
                    else if (op_name.rfind("shuttle_right") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                    }
                }
                else if (op_name.rfind("measurement", 0) == 0)
                {
                    // Measurement
                    
                    if (op_name.compare("measurement_left_up") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                        sites.push_back(sites[0] + n);
                    }
                    else if (op_name.compare("measurement_left_down") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                        sites.push_back(sites[0] - n);
                    }
                    else if (op_name.compare("measurement_right_up") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                        sites.push_back(sites[0] + n);
                    }
                    else if (op_name.compare("measurement_right_down") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                        sites.push_back(sites[0] - n);
                    }
                }
                else if (op_name.compare("sqswap") == 0
                        || op_name.compare("cphase") == 0)
                {
                    // SQSWAP or CPHASE
                }
                else
                {
                    // One-qubit gate
                    
                    // Add left or right site
                    if (op_name.rfind("_left") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                    }
                    else if (op_name.rfind("_right") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                    }
                }
            }
        }
        
        // Free memory
        delete crossbar_state;
        
        return kernels;
    }
    
    /**
     * Convert instructions operands from sites to qubits
     * 
     * Note: this also removes the unnecessary second site added in a previous step.
     * And this translations takes for granted that the scheduler has done good job.
     */
    ql::ir::bundles_t sites_to_qubits(ql::ir::bundles_t bundles, const ql::quantum_platform& platform)
    {
        IOUT("Translating sites to qubits");
        
        // Init crossbar state with initial placement
        crossbar_state_t* crossbar_state = this->get_init_crossbar_state(platform);
        
        for (auto & bundle : bundles)
        {
            for (auto & section : bundle.parallel_sections)
            {
                for (ql::gate* gate : section)
                {
                    std::string & op_name = gate->name;
                    std::vector<size_t> & operands = gate->operands;
                    
                    if (op_name.find("shuttle", 0) == 0)
                    {
                        // Shuttling
                        
                        // Remove additional operand
                        operands.pop_back();
                    }
                    else if (op_name.rfind("z_shuttle", 0) == 0
                        || op_name.rfind("s_shuttle", 0) == 0
                        || op_name.rfind("t_shuttle", 0) == 0)
                    {
                        // Z, S & T gate
                        
                        // Remove additional operand
                        operands.pop_back();
                    }
                    else if (op_name.rfind("measurement", 0) == 0)
                    {
                        // Measurement
                        
                        // Remove additional operand
                        operands.pop_back();
                        operands.pop_back();
                    }
                    else if (op_name.compare("sqswap") == 0
                        || op_name.compare("cphase") == 0)
                    {
                        // Do nothing
                    }
                    else
                    {
                        // One qubit gate
                        
                        // Remove additional operand
                        if (op_name.rfind("_left") == 0
                            || op_name.rfind("_right") == 0)
                        {
                            operands.pop_back();
                        }
                    }
                    
                    for (size_t key = 0; key < operands.size(); key++)
                    {
                        operands[key] = crossbar_state->get_qubit_by_site(operands[key]);
                    }

                    size_t qubit_index = operands[0];
                    
                    // Make the move
                    if (op_name.rfind("shuttle", 0) == 0)
                    {
                        // Shuttle

                        if (op_name.compare("shuttle_up") == 0)
                        {
                            crossbar_state->shuttle_up(qubit_index);
                        }
                        else if (op_name.compare("shuttle_down") == 0)
                        {
                            crossbar_state->shuttle_down(qubit_index);
                        }
                        else if (op_name.compare("shuttle_left") == 0)
                        {
                            crossbar_state->shuttle_left(qubit_index);
                        }
                        else if (op_name.compare("shuttle_right") == 0)
                        {
                            crossbar_state->shuttle_right(qubit_index);
                        }
                    }
                }
            }
        }
        
        return bundles;
    }
    
    /**
     * Init crossbar state with initial placement
     */
    crossbar_state_t* get_init_crossbar_state(const ql::quantum_platform& platform)
    {
        int m = platform.topology["y_size"];
        int n = platform.topology["x_size"];
        crossbar_state_t* crossbar_state = new crossbar_state_t(m, n);
        for (json::const_iterator it = platform.topology["init_configuration"].begin();
            it != platform.topology["init_configuration"].end(); ++it)
        {
            int key = std::stoi(it.key());
            std::string type = it.value()["type"];
            std::vector<int> pos = it.value()["position"];
            crossbar_state->add_qubit(pos[0], pos[1], key, (type.compare("ancilla") == 0));
        }
        
        return crossbar_state;
    }
    
    /**
     * Load the hardware settings
     */
    void load_hw_settings(const ql::quantum_platform& platform)
    {
        DOUT("Loading hardware settings ...");
        try
        {
            num_qubits = platform.hardware_settings["qubit_number"];
            ns_per_cycle = platform.hardware_settings["cycle_time"];
        }
        catch (json::exception e)
        {
            throw ql::exception("Error while reading hardware settings\n\t" + std::string(e.what()), false);
        }
    }

    /**
     * Write string to file
     */
    void write_to_file(std::string file_name, std::string ins_str)
    {
        std::string qasm_content("version 1.0\n");
        qasm_content += "# this file has been automatically generated by the OpenQL compiler please do not modify it manually.\n";
        qasm_content += "qubits " + std::to_string(num_qubits) + "\n\n";
        qasm_content += ".all_kernels";
        qasm_content += ins_str;
        
        std::ofstream fout;
        std::string file_path(ql::options::get("output_dir") + "/" + file_name + ".qasm");
        fout.open(file_path, ios::binary);
        if (fout.fail())
        {
            EOUT("Opening file " << file_name << std::endl
                << "Make sure the directory ("<< ql::options::get("output_dir") << ") exists");
            return;
        }
        fout << qasm_content << endl;
        fout.close();
    }
};

}
}
}

#endif // QL_CROSSBAR_QASM_COMPILER_H

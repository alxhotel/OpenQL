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
     */
    std::vector<quantum_kernel> qubits_to_sites(std::vector<quantum_kernel> kernels, const ql::quantum_platform& platform)
    {
        IOUT("Translating qubits to sites");
        
        // Init crossbar state with initial placement
        int m = platform.topology["y_size"];
        int n = platform.topology["x_size"];
        crossbar_state_t* crossbar_state = new crossbar_state_t(m, n);
        for (json::const_iterator it = platform.topology["configuration"].begin();
            it != platform.topology["configuration"].end(); ++it)
        {
            int key = std::stoi(it.key());
            std::string type = it.value()["type"];
            std::vector<int> pos = it.value()["position"];
            crossbar_state->add_qubit(pos[0], pos[1], key, (type.compare("ancilla") == 0));
        }
        
        for (auto &kernel : kernels)
        {
            ql::circuit& ckt = kernel.c;
            for (auto &ins : ckt)
            {
                std::string operation_name = ins->name;
                std::vector<size_t> & qubits = ins->operands;
                
                size_t qubit_index = qubits[0];
                
                // Translate qubits ti sites
                for (size_t key = 0; key < qubits.size(); key++)
                {
                    size_t qubit_index = qubits[key];
                    std::pair<size_t, size_t> pos = crossbar_state->get_position_by_qubit(qubit_index);
                    qubits[key] = (m * pos.first) + pos.second;
                }
                
                if (operation_name.rfind("shuttle", 0) == 0)
                {
                    // Shuttle
                    
                    std::vector<size_t> & sites = qubits;
                    if (operation_name.compare("shuttle_up") == 0)
                    {
                        sites.push_back(sites[0] + m);
                        crossbar_state->shuttle_up(qubit_index);
                    }
                    else if (operation_name.compare("shuttle_down") == 0)
                    {
                        sites.push_back(sites[0] - m);
                        crossbar_state->shuttle_down(qubit_index);
                    }
                    else if (operation_name.compare("shuttle_left") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                        crossbar_state->shuttle_left(qubit_index);
                    }
                    else if (operation_name.compare("shuttle_right") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                        crossbar_state->shuttle_right(qubit_index);
                    }
                }
                else if (operation_name.rfind("z_shuttle", 0) == 0
                        || operation_name.rfind("s_shuttle", 0) == 0
                        || operation_name.rfind("t_shuttle", 0) == 0)
                {
                    // Z, S & T Gate
                    
                    std::vector<size_t> & sites = qubits;
                    if (operation_name.rfind("shuttle_left") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                    }
                    else if (operation_name.rfind("shuttle_right") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                    }
                }
                else if (operation_name.rfind("measurement", 0) == 0)
                {
                    // Measurement
                    
                    std::vector<size_t> & sites = qubits;
                    if (operation_name.compare("measurement_left_up") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                        sites.push_back(sites[0] + m);
                    }
                    else if (operation_name.compare("measurement_left_down") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                        sites.push_back(sites[0] - m);
                    }
                    else if (operation_name.compare("measurement_right_up") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                        sites.push_back(sites[0] + m);
                    }
                    else if (operation_name.compare("measurement_right_down") == 0)
                    {
                        sites.push_back(sites[0] + 1);
                        sites.push_back(sites[0] - m);
                    }
                }
                else if (operation_name.compare("sqswap") == 0)
                {
                    // SQSWAP
                }
                else
                {
                    // One-qubit gate
                    
                    // Add left or right site
                    std::vector<size_t> & sites = qubits;
                    if (operation_name.rfind("_left") == 0)
                    {
                        sites.push_back(sites[0] - 1);
                    }
                    else if (operation_name.rfind("_right") == 0)
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
     */
    std::vector<quantum_kernel> sites_to_qubits(std::vector<quantum_kernel> kernels, const ql::quantum_platform& platform)
    {
        return kernels;
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

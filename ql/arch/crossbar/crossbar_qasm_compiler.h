/**
 * @file   crossbar_cqasm_compiler.h
 * @date   01/2019
 * @author Alejandro Morais
 * @brief  Crossbar cqasm compiler implementation
 */

#ifndef QL_CROSSBAR_QASM_COMPILER_H
#define QL_CROSSBAR_QASM_COMPILER_H

#include <ctime>
#include <cstdlib>

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
    ql::instruction_map_t instruction_map;

public:
    
    /**
     * Execute the mapping process
     */
    void map(std::string prog_name, std::vector<quantum_kernel>& kernels, const ql::quantum_platform& platform)
    {
        Mapper mapper;
        mapper.Init(platform);
        
        std::stringstream qasm_ins_str;
        for (auto &kernel : kernels)
        {
            mapper.Map(kernel);
            
            qasm_ins_str << "" + kernel.qasm() + "\n";
        }
        
        // Write to file
        std::string file_name = "" + prog_name + "_mapped";
        IOUT("Writing Crossbar cQASM mapped to " + file_name);
        write_to_file(file_name, qasm_ins_str.str());
    }
    
    /**
     * Pre-compile the qasm code
     */
    void pre_compile(std::string prog_name, std::vector<quantum_kernel> kernels, const ql::quantum_platform& plat)
    {
        IOUT("Pre-compiling kernels for Crossbar cQASM");
        
        // Crossbar HW params
        load_hw_settings(platform);
        
        // Translate virtual qubits to sites
        if (ql::options::get("mapper") == "no")
        {
            // Mapping (virtual to real) is one-to-one
            
            // HACK: remove const modifier
            //ql::quantum_platform& platform = const_cast<ql::quantum_platform&>(plat);
            
            // Convert real qubits to sites
            //kernels = real_to_sites(kernels, platform);
        }
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

        // Fix: add ancilla to measurements
        add_virtual_ancilla(prog_name, kernels, platform);
        
        if (ql::options::get("mapper") != "no")
        {
            DOUT("Mapping...");
            
            // Do routing
            map(prog_name, kernels, platform);

            DOUT("MAPPING DONE !!");

            divide_kernels(prog_name, kernels, platform);

            DOUT("DIVISION OF KERNELS DONE !!");
            
            // Fake sites to sites
            fake_sites_to_real_qubits(prog_name, kernels, platform);
            
            DOUT("FAKE SITES TO REAL QUBITS DONE !!");

            //exit(1);
        }
        
        // Dynamic decomposition
        dynamic_mapping_decompose(prog_name, kernels, platform);

        DOUT("DYNAMIC DECOMPOSITION DONE !!");
        
        // For first iteration
        crossbar_state_t* temp_initial_crossbar_state;
        crossbar_state_t* temp_final_crossbar_state = this->get_init_crossbar_state(platform);

        size_t total_depth = 0;
        std::stringstream before_qasm_ins_str;
        std::stringstream qasm_ins_str;
        for (auto& kernel : kernels)
        {
            IOUT("Compiling kernel: " << kernel.name);
            ql::circuit& ckt = kernel.c;

            if (!ckt.empty())
            {
                temp_initial_crossbar_state = temp_final_crossbar_state;
                temp_final_crossbar_state = this->get_final_crossbar_state(temp_initial_crossbar_state, kernel);

                size_t num_sites = temp_final_crossbar_state->get_total_sites();

                // Schedule with platform resource constraints
                ql::ir::bundles_t bundles = crossbar_scheduler::schedule_rc(
                    ckt, platform,
                    temp_initial_crossbar_state, temp_final_crossbar_state,
                    num_sites
                );

                // Translate sites back to qubits
                sites_to_real(bundles, temp_initial_crossbar_state);

                before_qasm_ins_str << "" + ql::ir::qasm(bundles) + "\n";

                // Decompose instructions for SINGLE GATES
                decompose_single_gates(bundles, temp_initial_crossbar_state);

                // Convert to string
                qasm_ins_str << "" + ql::ir::qasm(bundles) + "\n";

                total_depth += kernel.get_depth();
            }
        }

        std::string b_file_name = "" + prog_name + "_b_compiled";
        IOUT("Writing Crossbar cQASM compiled to " + b_file_name);
        write_to_file(b_file_name, before_qasm_ins_str.str());

        std::string file_name = "" + prog_name + "_compiled";
        IOUT("Writing Crossbar cQASM compiled to " + file_name);
        qasm_ins_str << "\n\n# Total depth: " << std::to_string(total_depth) << "\n";
        write_to_file(file_name, qasm_ins_str.str());

        DOUT("Compilation of Crossbar cQASM done");
    }

private:

    bool is_single_gate(std::string name)
    {
        return !(name.compare("swap") == 0 || name.compare("move") == 0
            || name.compare("sqswap") == 0 || name.compare("cz") == 0
            || name.find("shuttle_", 0) != std::string::npos
            || name.find("z_", 0) != std::string::npos || name.find("zdag_", 0) != std::string::npos
            || name.find("s_", 0) != std::string::npos || name.find("sdag_", 0) != std::string::npos
            || name.find("t_", 0) != std::string::npos || name.find("tdag_", 0) != std::string::npos
            || name.find("measure", 0) != std::string::npos
        );
    }
    
    ql::gate* get_inverse_gate(ql::gate* gate)
    {
        // TODO
        ql::custom_gate* new_gate = new ql::custom_gate(gate->name);
        new_gate->operands = gate->operands;
        new_gate->creg_operands = gate->creg_operands;
        new_gate->angle = gate->angle;
        return new_gate;
        /*switch (gate->name.c_str())
        {
            case "x":
                return new ql::gate("x");
            case "y":
                return new ql::gate("x");
            case "z":
                return new ql::gate("z");
            case "h":
                return new ql::gate("h");
        }*/
    }
    
    ql::gate* get_gate(std::string name, std::vector<size_t> operands, const ql::quantum_platform& platform)
    {
        std::map<std::string, ql::custom_gate*> gate_definition = platform.instruction_map;
        if (gate_definition.find(name) != gate_definition.end())
        {
            ql::custom_gate* gate = new ql::custom_gate(*gate_definition[name]);
            for (auto& operand : operands)
            {
                gate->operands.push_back(operand);
            }
            
            return gate;
        }
        
        throw std::runtime_error("Can not find gate: " + name);
    }
    
    void add_virtual_ancilla(std::string prog_name, std::vector<quantum_kernel>& kernels, const ql::quantum_platform& platform)
    {
        // TODO: add ancilla for measurement
        
    }
    
    int get_qubit_from_col_parity(std::pair<size_t, size_t> pos, crossbar_state_t* crossbar_state)
    {
        size_t parity = pos.second % 2;
        for (size_t j = parity; j < crossbar_state->get_x_size(); j = j + 2)
        {
            for (size_t i = 0; i < crossbar_state->get_y_size(); i++)
            {
                if (i == pos.first && j == pos.second)
                {
                    continue;
                }
                
                if (crossbar_state->get_count_by_position(i, j) > 0)
                {
                    return crossbar_state->get_qubit_by_pos(i, j);
                }
            }
        }
        
        return -1;
    }
    
    void decompose_single_gates(ql::ir::bundles_t& bundles, crossbar_state_t* crossbar_state)
    {
        ql::circuit new_circuit;
        
        for (auto& bundle : bundles)
        {
            std::vector<ql::gate*> new_gates;
            
            for (auto& section : bundle.parallel_sections)
            {
                for (auto& gate : section)
                {
                    if (this->is_single_gate(gate->name))
                    {
                        DOUT("SINGLE: " + gate->name);
                        
                        std::vector<size_t> qubits = gate->operands;
                        std::pair<size_t, size_t> pos = crossbar_state->get_pos_by_qubit(qubits[0]);
                        
                        std::vector<std::pair<std::string, std::vector<size_t> > > gate_params;
                        
                        // Original gate
                        gate_params.push_back(std::make_pair(gate->name, gate->operands));
                        
                        // Find site occupied by qubit in parity of original site
                        int qubit = this->get_qubit_from_col_parity(pos, crossbar_state);
                        if (qubit != -1)
                        {
                            // Shuttle out                        
                            if (pos.second > 0 && crossbar_state->get_count_by_position(pos.first, pos.second) == 0)
                            {
                                std::vector<size_t> new_vector = {qubits[0]};
                                gate_params.push_back(std::make_pair("shuttle_left", new_vector));
                            }
                            else
                            {
                                std::vector<size_t> new_vector = {qubits[0]};
                                gate_params.push_back(std::make_pair("shuttle_right", new_vector));
                            }
                            
                            // Inverse gate
                            // TODO: use custom_gate instead of name (in pair) for a more realistic apporach
                            std::vector<size_t> new_vector_1 = {(size_t) qubit};
                            gate_params.push_back(make_pair(this->get_inverse_gate(gate)->name, new_vector_1));
                            
                            // Shuttle back
                            if (pos.second > 0 && crossbar_state->get_count_by_position(pos.first, pos.second) == 0)
                            {
                                std::vector<size_t> new_vector = {qubits[0]};
                                gate_params.push_back(std::make_pair("shuttle_right", new_vector));
                            }
                            else
                            {
                                std::vector<size_t> new_vector = {qubits[0]};
                                gate_params.push_back(std::make_pair("shuttle_left", new_vector));
                            }
                        }
                        
                        size_t cycle = gate->cycle;
                        for (auto& gate_param : gate_params)
                        {
                            // Get definition
                            ql::custom_gate* gate_def = this->instruction_map[gate_param.first];
                            // New gate
                            ql::custom_gate* new_gate = new ql::custom_gate(gate_def->name);
                            new_gate->operands = gate_param.second;
                            new_gate->cycle = cycle;
                            new_gate->duration = gate_def->duration;
                            new_gates.push_back(new_gate);
                            
                            cycle = cycle + (new_gate->duration / ns_per_cycle);
                        }
                    }
                    else
                    {
                        new_gates.push_back(gate);
                    }
                }
            }
            
            for (auto& gate : new_gates)
            {
                new_circuit.push_back(gate);
            }
            
            // Execute
            this->execute_gates(new_gates, crossbar_state);
        }
        
        // Change the bundles
        bundles = ql::ir::bundle(new_circuit, ns_per_cycle);
    }
    
    void divide_kernels(std::string prog_name, std::vector<quantum_kernel>& kernels, const ql::quantum_platform& const_platform)
    {
        int i = 0;
        std::string new_name;
        ql::quantum_kernel* new_kernel;
        std::vector<ql::quantum_kernel> new_kernels;
        ql::quantum_platform& platform = const_cast<ql::quantum_platform&>(const_platform);
        
        for (auto& kernel : kernels)
        {
            // Create a new kernel
            new_name = kernel.get_name() + "_" + std::to_string(i++);
            new_kernel = new ql::quantum_kernel(new_name, platform, kernel.qubit_count, kernel.creg_count);
            
            ql::circuit& circ = kernel.get_circuit();
            for (auto& gate : circ)
            {
                std::string name = gate->name;
                
                if (gate->name.compare("swap") == 0 || gate->name.compare("move") == 0
                    || gate->name.compare("sqswap") == 0 || gate->name.compare("cz") == 0)
                {
                    if (new_kernel->get_circuit().size() > 0)
                    {
                        // Backup kernel
                        new_kernels.push_back(*new_kernel);
                        // New kernel
                        new_name = kernel.get_name() + "_" + std::to_string(i++);
                        new_kernel = new ql::quantum_kernel(new_name, platform, kernel.qubit_count, kernel.creg_count);
                    }
                    
                    new_kernel->get_circuit().push_back(gate);
                    
                    // Backup kernel
                    new_kernels.push_back(*new_kernel);
                    // New kernel
                    new_name = kernel.get_name() + "_" + std::to_string(i++);
                    new_kernel = new ql::quantum_kernel(new_name, platform, kernel.qubit_count, kernel.creg_count);
                }
                else
                {
                    // TODO: push a clone of gate
                    new_kernel->get_circuit().push_back(gate);
                }
            }
            
            // Backup kernel
            new_kernels.push_back(*new_kernel);
        }
        
        // Set to new kernels
        kernels = new_kernels;
        if (kernels.back().get_circuit().size() == 0)
        {
            kernels.pop_back();
        }

        std::stringstream qasm_ins_str;
        for (auto& kernel : kernels)
        {
            qasm_ins_str << "" + kernel.qasm() + "\n";
        }
        
        std::string file_name = "" + prog_name + "_divided";
        IOUT("Writing Crossbar cQASM divided to " + file_name);
        write_to_file(file_name, qasm_ins_str.str());
    }
    
    /**
     * FAKE SITES => REAL QUBITS
     */
    std::vector<quantum_kernel> fake_sites_to_real_qubits(std::string prog_name,
        std::vector<quantum_kernel>& kernels, const ql::quantum_platform& platform)
    {
        IOUT("Translating fake sites to real qubits");
        
        // Init crossbar state with initial placement
        crossbar_state_t* crossbar_state = this->get_init_crossbar_state(platform);
        
        // Set new number of "qubits"
        //platform.qubit_number = crossbar_state->get_count_qubits();
        
        for (auto &kernel : kernels)
        {
            // Set new number of "qubits"
            kernel.qubit_count = crossbar_state->get_count_qubits();
            ql::circuit& ckt = kernel.c;
            
            for (auto& ins : ckt)
            {
                std::string op_name = ins->name;
                std::vector<size_t>& fake_sites = ins->operands;
                std::vector<size_t>& qubits = ins->operands;

                // Translate fake sites ti sites
                for (size_t key = 0; key < fake_sites.size(); key++)
                {
                    qubits[key] = crossbar_state->get_qubit_by_fake_site(fake_sites[key]);
                }
                
                // Execute
                if (op_name.compare("swap") == 0 || op_name.compare("move") == 0)
                {
                    crossbar_state->swap_qubits(qubits[0], qubits[1]);
                }
            }
        }
        
        // Free memory
        delete crossbar_state;
        
        return kernels;
    }

    void dynamic_mapping_decompose(std::string prog_name, std::vector<quantum_kernel>& kernels, const ql::quantum_platform& platform)
    {
        // TODO
        
        crossbar_state_t* crossbar_state = this->get_init_crossbar_state(platform);
        
        size_t n = crossbar_state->get_x_size();
        
        std::stringstream qasm_ins_str;
        for (auto& kernel : kernels)
        {
            ql::circuit& circ = kernel.c;
            for (auto it = circ.begin(); it != circ.end(); )
            {
                ql::gate* gate = (*it);
                std::string name = gate->name;
                std::vector<size_t>& qubits = gate->operands;
                std::pair<size_t, size_t> pos_a = crossbar_state->get_pos_by_qubit(qubits[0]);

                ql::circuit new_ins;
                
                DOUT(name.c_str());
                
                // Decompose swaps into shuttles
                if (name.compare("swap") == 0 || name.compare("move") == 0)
                {
                    // Check position of qubits
                    std::pair<size_t, size_t> pos_b = crossbar_state->get_pos_by_qubit(qubits[1]);

                    if (pos_a.first < pos_b.first)
                    {
                        new_ins.push_back(this->get_gate("shuttle_up", {qubits[0]}, platform));
                        new_ins.push_back(this->get_gate("shuttle_down", {qubits[1]}, platform));
                    }
                    else
                    {
                        new_ins.push_back(this->get_gate("shuttle_up", {qubits[1]}, platform));
                        new_ins.push_back(this->get_gate("shuttle_down", {qubits[0]}, platform));
                    }
                    
                    if (pos_a.second < pos_b.second)
                    {
                        new_ins.push_back(this->get_gate("shuttle_left", {qubits[1]}, platform));
                        new_ins.push_back(this->get_gate("shuttle_right", {qubits[0]}, platform));
                    }
                    else
                    {
                        new_ins.push_back(this->get_gate("shuttle_left", {qubits[0]}, platform));
                        new_ins.push_back(this->get_gate("shuttle_right", {qubits[1]}, platform));
                    }
                }
                else if (name.compare("sqswap") == 0 || name.compare("cz") == 0)
                {
                    // Decompose SQSWAP and CZ
                    std::pair<size_t, size_t> pos_b = crossbar_state->get_pos_by_qubit(qubits[1]);

                    if (name.compare("sqswap") == 0)
                    {
                        // SQSWAP
                        if (pos_a.second < pos_b.second)
                        {
                            new_ins.push_back(this->get_gate("shuttle_left", {qubits[1]}, platform));
                            new_ins.push_back(this->get_gate(name, {qubits[0], qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("shuttle_right", {qubits[1]}, platform));
                        }
                        else
                        {
                            new_ins.push_back(this->get_gate("shuttle_left", {qubits[0]}, platform));
                            new_ins.push_back(this->get_gate(name, {qubits[0], qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("shuttle_right", {qubits[0]}, platform));
                        }
                    }
                    else
                    {
                        // CZ
                        if (pos_a.first < pos_b.first)
                        {
                            new_ins.push_back(this->get_gate("shuttle_down", {qubits[1]}, platform));
                            new_ins.push_back(this->get_gate(name, {qubits[0], qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("shuttle_up", {qubits[1]}, platform));
                        }
                        else
                        {
                            new_ins.push_back(this->get_gate("shuttle_down", {qubits[0]}, platform));
                            new_ins.push_back(this->get_gate(name, {qubits[0], qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("shuttle_up", {qubits[0]}, platform));
                        }
                    }
                }
                else if (name.rfind("z") == 0 || name.rfind("s") == 0 || name.rfind("t") == 0)
                {
                    // Decompose Z, S & T into shuttles
                    /*if (pos_a.second > 0 && pos_a.second < n - 1)
                    {
                        // Random
                        srand(time(0));
                        int randomval = rand() % 2;
                        
                        if (randomval == 0)
                        {
                            // Left
                            new_ins.push_back(this->get_gate(name + "_shuttle_left", {qubits[0]}, platform));
                        }
                        else
                        {
                            // Right
                            new_ins.push_back(this->get_gate(name + "_shuttle_right", {qubits[0]}, platform));
                        }
                    }
                    else */if (pos_a.second > 0) {
                        // Left
                        new_ins.push_back(this->get_gate(name + "_shuttle_left", {qubits[0]}, platform));
                    }
                    else
                    {
                        // Right
                        new_ins.push_back(this->get_gate(name + "_shuttle_right", {qubits[0]}, platform));
                    }
                }
                else if (name.rfind("measure") == 0)
                {
                    // TODO: Decompose measurement into PBS and readout
                    std::pair<size_t, size_t> pos_b = crossbar_state->get_pos_by_qubit(qubits[1]);
                    
                    if (pos_a.first < pos_b.first)
                    {
                        if (pos_a.second < pos_b.second)
                        {
                            new_ins.push_back(this->get_gate("shuttle_down", {qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("measure_right_up", {qubits[0], qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("shuttle_up", {qubits[1]}, platform));
                        }
                        else
                        {
                            new_ins.push_back(this->get_gate("shuttle_up", {qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("measure_right_down", {qubits[0], qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("shuttle_down", {qubits[1]}, platform));
                        }
                    }
                    else
                    {
                        if (pos_a.second < pos_b.second)
                        {
                            new_ins.push_back(this->get_gate("shuttle_down", {qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("measure_left_up", {qubits[0], qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("shuttle_up", {qubits[1]}, platform));
                        }
                        else
                        {
                            new_ins.push_back(this->get_gate("shuttle_up", {qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("measure_left_down", {qubits[0], qubits[1]}, platform));
                            new_ins.push_back(this->get_gate("shuttle_down", {qubits[1]}, platform));
                        }
                    }
                }
                else
                {
                    // (Later we will) Decompose single qubit gates into "shuttle"-like strategy
                    new_ins.push_back(this->get_gate(name, {qubits[0]}, platform));
                }
                
                // Make replacement
                if (new_ins.size() > 0)
                {
                    it = circ.erase(it);
                    it = circ.insert(it, new_ins.begin(), new_ins.end());
                    it = it + new_ins.size();
                    
                    // Transform fake real to sites & execute any shuttle
                    transform_and_execute(new_ins, crossbar_state);
                }
                else
                {
                    it++;
                }
            }
            
            kernel.qubit_count = crossbar_state->get_total_sites();
            qasm_ins_str << "" + kernel.qasm() + "\n";
        }
        
        delete crossbar_state;
        
        std::string file_name = "" + prog_name + "_decomposed";
        IOUT("Writing Crossbar cQASM decomposed to " + file_name);
        write_to_file(file_name, qasm_ins_str.str());
    }
    
    /**
     * QUBITS => SITES
     */
    void transform_and_execute(ql::circuit& ckt, crossbar_state_t* crossbar_state)
    {
        int n = crossbar_state->get_x_size();
        
        for (auto &ins : ckt)
        {
            std::string op_name = ins->name;
            std::vector<size_t>& qubits = ins->operands;
            std::vector<size_t>& sites = ins->operands;
            
            size_t qubit_index = qubits[0];
            
            // Translate qubits ti sites
            std::string qubit_str = "";
            std::string sites_str = "";
            for (size_t key = 0; key < qubits.size(); key++)
            {
                if (key > 0)
                {
                    qubit_str = qubit_str + std::string(" ");
                    sites_str = sites_str + std::string(" ");
                }
                qubit_str = qubit_str + std::to_string(qubits[key]);
                sites[key] = crossbar_state->get_site_by_qubit(qubits[key]);
                sites_str = sites_str + std::to_string(sites[key]);
            }
            
            DOUT(std::string("Converting: ") + op_name
                    + " q " + qubit_str + " -> s " + sites_str
            );

            if (op_name.rfind("shuttle_") == 0)
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
            else if (op_name.rfind("_shuttle") != std::string::npos)
            {
                // Z, S & T Gate
                
                if (op_name.rfind("shuttle_left") != std::string::npos)
                {
                    sites.push_back(sites[0] - 1);
                }
                else if (op_name.rfind("shuttle_right") != std::string::npos)
                {
                    sites.push_back(sites[0] + 1);
                }
            }
            else if (op_name.rfind("measurement") != std::string::npos)
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
            else if (op_name.compare("sqswap") == 0 || op_name.compare("cz") == 0)
            {
                // SQSWAP or CZ
            }
            else
            {
                // One-qubit gate

                // Add left or right site
                if (op_name.rfind("_left") != std::string::npos)
                {
                    sites.push_back(sites[0] - 1);
                }
                else if (op_name.rfind("_right") != std::string::npos)
                {
                    sites.push_back(sites[0] + 1);
                }
            }
        }
    }
    
    /**
     * Convert instructions operands from real qubits to crossbar sites
     * 
     * Note: this also adds a second site for the dependency graph
     */
    std::vector<quantum_kernel> real_to_sites(std::vector<quantum_kernel> kernels, ql::quantum_platform& platform)
    {
        IOUT("Translating real qubits to sites");
        
        // Init crossbar state with initial placement
        crossbar_state_t* crossbar_state = this->get_init_crossbar_state(platform);
        
        // Set new number of "qubits" (= number of sites)
        platform.qubit_number = crossbar_state->get_x_size() * crossbar_state->get_y_size();
        
        for (auto &kernel : kernels)
        {
            // Set new number of "qubits"
            kernel.qubit_count = crossbar_state->get_x_size() * crossbar_state->get_y_size();
            
            ql::circuit& ckt = kernel.c;
            
            transform_and_execute(ckt, crossbar_state);
        }
        
        // Free memory
        delete crossbar_state;
        
        return kernels;
    }
    
    void transform_sites_to_real(ql::gate* gate, crossbar_state_t* crossbar_state)
    {
        std::string& op_name = gate->name;
        std::vector<size_t>& operands = gate->operands;

        if (op_name.rfind("shuttle") == 0)
        {
            // Shuttling

            // Remove additional operand
            operands.pop_back();
        }
        else if (op_name.rfind("_shuttle") != std::string::npos)
        {
            // Z, S & T gate

            // Remove additional operand
            operands.pop_back();
        }
        else if (op_name.rfind("measurement") != std::string::npos)
        {
            // Measurement

            // Remove additional operand
            operands.pop_back();
            operands.pop_back();
        }
        else if (op_name.compare("sqswap") == 0 || op_name.compare("cz") == 0)
        {
            // Do nothing
        }
        else
        {
            // One qubit gate

            // Remove additional operand
            if (op_name.rfind("_left") != std::string::npos
                || op_name.rfind("_right") != std::string::npos)
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
        if (op_name.rfind("shuttle") == 0)
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
    
    /**
     * Convert instructions operands from sites to qubits
     * 
     * Note: this also removes the unnecessary second site added in a previous step.
     * And this translations takes for granted that the scheduler has done good job.
     */
    ql::ir::bundles_t sites_to_real(ql::ir::bundles_t& bundles, crossbar_state_t* crossbar_state)
    {
        IOUT("Translating sites to qubits");
        
        for (auto& bundle : bundles)
        {
            for (auto& section : bundle.parallel_sections)
            {
                for (ql::gate* gate : section)
                {
                    transform_sites_to_real(gate, crossbar_state);
                }
            }
        }
        
        return bundles;
    }
    
    void execute_gates(std::vector<ql::gate*> gates, crossbar_state_t* crossbar_state)
    {
        for (auto& gate : gates)
        {
            std::string op_name = gate->name;
            size_t qubit_index = gate->operands[0];
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
    
    /**
     * Init crossbar state with initial placement
     */
    crossbar_state_t* get_init_crossbar_state(const ql::quantum_platform& platform)
    {
        crossbar_state_t* initial_crossbar_state;
        
        // Initialize the board state
        if (platform.topology.count("x_size") > 0
            && platform.topology.count("y_size") > 0)
        {
            initial_crossbar_state = new crossbar_state_t(
                platform.topology["y_size"],
                platform.topology["x_size"]
            );
        }
        else
        {
            COUT("Error: Grid topology for the crossbar was not defined");
            throw ql::exception("[x] Error: Grid topology for the crossbar was not defined!", false);
        }
        
        // Initialize the configuration
        if (platform.topology.count("init_configuration") > 0)
        {
            for (json::const_iterator it = platform.topology["init_configuration"].begin();
                it != platform.topology["init_configuration"].end(); ++it)
            {
                int key = std::stoi(it.key());
                std::string type = it.value()["type"];
                std::vector<int> value = it.value()["position"];
                initial_crossbar_state->add_qubit(value[0], value[1], key, (type.compare("ancilla") == 0));
            }
        }
        else
        {
            COUT("Error: Qubit init placement for the crossbar were not defined");
            throw ql::exception("[x] Error: Qubit init placement for the crossbar were not defined!", false);
        }
        
        return initial_crossbar_state;
    }
    
    crossbar_state_t* get_final_crossbar_state(crossbar_state_t* initial_crossbar_state,
        ql::quantum_kernel kernel)
    {
        std::vector<ql::quantum_kernel> kernels;
        kernels.push_back(kernel);
        return get_final_crossbar_state(initial_crossbar_state, kernels);
    }
    
    crossbar_state_t* get_final_crossbar_state(crossbar_state_t* initial_crossbar_state,
        std::vector<ql::quantum_kernel> kernels)
    {
        crossbar_state_t* final_crossbar_state = initial_crossbar_state->clone();
        for (auto& kernel : kernels)
        {
            ql::circuit & ckt = kernel.c;
            for (auto& ins : ckt)
            {
                std::string operation_name = ins->name;
                std::vector<size_t>& operands = ins->operands;
                
                size_t site_index = final_crossbar_state->get_qubit_by_site(operands[0]);

                if (operation_name.compare("shuttle_up") == 0)
                {
                    final_crossbar_state->shuttle_up(site_index);
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    final_crossbar_state->shuttle_down(site_index);
                }
                else if (operation_name.compare("shuttle_left") == 0)
                {
                    final_crossbar_state->shuttle_left(site_index);
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    final_crossbar_state->shuttle_right(site_index);
                }
            }
        }
        
        return final_crossbar_state;
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
            instruction_map = platform.instruction_map;
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
        qasm_content += std::string(".all_kernels") + "\n";
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

/**
 * @file   quantumsim_eqasm_compiler.h
 * @date   03/2018
 * @author Imran Ashraf
 * @brief  quantumsim compiler implementation
 */

#ifndef QL_QUANTUMSIM_EQASM_COMPILER_H
#define QL_QUANTUMSIM_EQASM_COMPILER_H

#include <ql/platform.h>
#include <ql/kernel.h>
#include <ql/gate.h>
#include <ql/ir.h>
#include <ql/scheduler.h>
#include <ql/eqasm_compiler.h>
#include <ql/mapper.h>

namespace ql
{
namespace arch
{

class quantumsim_eqasm_compiler : public eqasm_compiler
{
public:
    size_t num_qubits;
    size_t ns_per_cycle;

private:
      size_t get_qubit_usecount(std::vector<quantum_kernel>& kernels)
      {
         std::vector<size_t> usecount;
         usecount.resize(num_qubits, 0);
         for (auto& k: kernels)
         {
            for (auto & gp: k.c)
            {
                switch(gp->type())
                {
                case __classical_gate__:
                case __wait_gate__:
                    break;
                default:
                    for (auto v: gp->operands)
                    {
                        usecount[v]++;
                    }
                    break;
                }
            }
         }
         size_t count= 0;
         for (auto v: usecount)
         {
            if (v != 0)
            {
                count++;
            }
         }
         return count;
      }

    void write_qasm(std::stringstream& fname, std::vector<quantum_kernel>& kernels, const ql::quantum_platform& platform)
    {
        size_t total_depth = 0;
        size_t total_quantum_gates = 0;
        size_t total_classical_operations = 0;
        size_t total_non_single_qubit_gates= 0;
        size_t total_swaps = 0;
        size_t total_moves = 0;
        std::stringstream out_qasm;
        out_qasm << "version 1.0\n";
        out_qasm << "# this file has been automatically generated by the OpenQL compiler please do not modify it manually.\n";
        out_qasm << "qubits " << platform.qubit_number << "\n";
        for(auto &kernel : kernels)
        {
            if (kernel.bundles.empty())
            {
                out_qasm << kernel.qasm();
            }
            else
            {
                out_qasm << "\n" << kernel.get_prologue();
                out_qasm << ql::ir::qasm(kernel.bundles);
                out_qasm << kernel.get_epilogue();
            }
            total_depth += kernel.get_depth();
            total_classical_operations += kernel.get_classical_operations_count();
            total_quantum_gates += kernel.get_quantum_gates_count();
            total_non_single_qubit_gates += kernel.get_non_single_qubit_quantum_gates_count();
            total_swaps += kernel.swaps_added;
            total_moves += kernel.moves_added;
        }
        out_qasm << "\n";
        out_qasm << "# Total depth: " << total_depth << "\n";
        out_qasm << "# Total no. of quantum gates: " << total_quantum_gates << "\n";
        out_qasm << "# Total no. of non single qubit gates: " << total_non_single_qubit_gates << "\n";
        out_qasm << "# Total no. of swaps: " << total_swaps << "\n";
        out_qasm << "# Total no. of moves of swaps: " << total_moves << "\n";
        out_qasm << "# Total no. of classical operations: " << total_classical_operations << "\n";
        out_qasm << "# Qubits used: " << get_qubit_usecount(kernels) << "\n";
        out_qasm << "# No. kernels: " << kernels.size() << "\n";
        ql::utils::write_file(fname.str(), out_qasm.str());
    }

    void map(std::string& prog_name, std::vector<quantum_kernel>& kernels, const ql::quantum_platform& platform)
    {
        for(auto &kernel : kernels)
        {
            // don't trust the cycle fields in the instructions
            // and let write_qasm print the circuit instead of the bundles
            kernel.bundles.clear();
        }

        std::stringstream mapper_in_fname;
        mapper_in_fname << ql::options::get("output_dir") << "/" << prog_name << "_mapper_in.qasm";
        IOUT("writing mapper input qasm to '" << mapper_in_fname.str() << "' ...");
        write_qasm(mapper_in_fname, kernels, platform);

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
            mapper.MapCircuit(kernel);
                            // kernel.c, kernel.name, kernel.qubit_count, kernel.creg_count);
                            // kernel.qubit_count is number of virtual qubits, i.e. highest indexed qubit minus 1
                            // and kernel.qubit_count is updated to real highest index used minus -1
            kernel.bundles = mapper.Bundler(kernel);
        }
        std::stringstream mapper_out_fname;
        mapper_out_fname << ql::options::get("output_dir") << "/" << prog_name << "_mapper_out.qasm";
        IOUT("writing mapper output qasm to '" << mapper_out_fname.str() << "' ...");
        write_qasm(mapper_out_fname, kernels, platform);
    }

    ql::ir::bundles_t quantumsim_schedule_rc(ql::circuit & ckt, 
        const ql::quantum_platform & platform, size_t nqubits, size_t ncreg = 0)
    {
        IOUT("Resource constraint scheduling for quantumsim ...");
    
        scheduling_direction_t  direction;
        std::string schedopt = ql::options::get("scheduler");
        if ("ASAP" == schedopt)
        {
            direction = forward_scheduling;
        }
        else if ("ALAP" == schedopt)
        {
            direction = backward_scheduling;
        }
        else
        {
            EOUT("Unknown scheduler");
            throw ql::exception("Unknown scheduler!", false);
    
        }
        resource_manager_t rm(platform, direction);
    
        Scheduler sched;
        sched.Init(ckt, platform, nqubits, ncreg);
        ql::ir::bundles_t bundles;
        if ("ASAP" == schedopt)
        {
            bundles = sched.schedule_asap(rm, platform);
        }
        else if ("ALAP" == schedopt)
        {
            bundles = sched.schedule_alap(rm, platform);
        }
        else
        {
            EOUT("Unknown scheduler");
            throw ql::exception("Unknown scheduler!", false);
    
        }
    
        IOUT("Resource constraint scheduling for quantumsim [Done].");
        return bundles;
    }

    void schedule(std::string& prog_name, std::vector<quantum_kernel>& kernels, const ql::quantum_platform& platform)
    {
        for(auto &kernel : kernels)
        {
            IOUT("Scheduling kernel: " << kernel.name);
            if (! kernel.c.empty())
            {
                auto num_creg = 0;  // quantumsim
                kernel.bundles = quantumsim_schedule_rc(kernel.c, platform, num_qubits, num_creg);
            }
        }
        std::stringstream rcscheduler_out_fname;
        rcscheduler_out_fname << ql::options::get("output_dir") << "/" << prog_name << "_rcscheduler_out.qasm";
        IOUT("writing rcscheduler output qasm to '" << rcscheduler_out_fname.str() << "' ...");
        write_qasm(rcscheduler_out_fname, kernels, platform);
    }

public:
    /*
     * compile qasm to quantumsim
     */
    // program level compilation
    void compile(std::string prog_name, std::vector<quantum_kernel> kernels, const ql::quantum_platform& platform)
    {
        IOUT("Compiling " << kernels.size() << " kernels to generate quantumsim eQASM ... ");

        std::string params[] = { "qubit_number", "cycle_time" };
        size_t p = 0;
        try
        {
            num_qubits      = platform.hardware_settings[params[p++]];
            ns_per_cycle    = platform.hardware_settings[params[p++]];
        }
        catch (json::exception &e)
        {
            throw ql::exception("[x] error : ql::quantumsim::compile() : error while reading hardware settings : parameter '"+params[p-1]+"'\n\t"+ std::string(e.what()),false);
        }

        write_quantumsim_program(prog_name, num_qubits, kernels, platform, "");

        map(prog_name, kernels, platform);

        schedule(prog_name, kernels, platform);

        // write scheduled bundles for quantumsim
        write_quantumsim_program(prog_name, num_qubits, kernels, platform, "mapped");

        DOUT("Compiling CCLight eQASM [Done]");
    }

private:
    // write scheduled bundles for quantumsim
    void write_quantumsim_program( std::string prog_name, size_t num_qubits,
        std::vector<quantum_kernel>& kernels, const ql::quantum_platform & platform, std::string suffix)
    {
        IOUT("Writing scheduled Quantumsim program");
        ofstream fout;
        string qfname( ql::options::get("output_dir") + "/" + prog_name + "_quantumsim_" + suffix + ".py");
        IOUT("Writing scheduled Quantumsim program to " << qfname);
        fout.open( qfname, ios::binary);
        if ( fout.fail() )
        {
            EOUT("opening file " << qfname << std::endl
                     << "Make sure the output directory ("<< ql::options::get("output_dir") << ") exists");
            return;
        }

        fout << "# Quantumsim program generated OpenQL\n"
             << "# Please modify at your will to obtain extra information from Quantumsim\n\n";

        fout << "import numpy as np\n"
             << "from quantumsim.circuit import Circuit\n"
             << "from quantumsim.circuit import uniform_noisy_sampler\n"
             << endl;

        fout << "from quantumsim.circuit import IdlingGate as i\n"                         
             << "from quantumsim.circuit import RotateY as ry\n"                            
             << "from quantumsim.circuit import RotateX as rx\n"                            
             << "from quantumsim.circuit import RotateZ as rz\n"                            
             << "from quantumsim.circuit import Hadamard as h\n"                            
             << "from quantumsim.circuit import CPhase as cz\n"                             
             << "from quantumsim.circuit import CNOT as cnot\n"                             
             << "from quantumsim.circuit import Swap as swap\n"                             
             << "from quantumsim.circuit import CPhaseRotation as cr\n"                     
             << "from quantumsim.circuit import ConditionalGate as ConditionalGate\n"       
             << "from quantumsim.circuit import RotateEuler as RotateEuler\n"               
             << "from quantumsim.circuit import ResetGate as ResetGate\n"                   
             << "from quantumsim.circuit import Measurement as measure\n"                   
             << "import quantumsim.sparsedm as sparsedm\n"                                  
             << "\n"                                                                        
             << "# print('GPU is used:', sparsedm.using_gpu)\n"                             
             << "\n"                                                                        
             << "\n"                                                                        
             << "def t(q, time):\n"                                                         
             << "    return RotateEuler(q, time=time, theta=0, phi=np.pi/4, lamda=0)\n"     
             << "\n"                                                                        
             << "def tdag(q, time):\n"                                                      
             << "    return RotateEuler(q, time=time, theta=0, phi=-np.pi/4, lamda=0)\n"    
             << "\n"                                                                        
             << "def measure_z(q, time, sampler):\n"                                        
             << "    return measure(q, time, sampler)\n"                                    
             << "\n"                                                                        
             << "def z(q, time):\n"                                                         
             << "    return rz(q, time, angle=np.pi)\n"                                     
             << "\n"                                                                        
             << "def x(q, time):\n"                                                         
             << "    return rx(q, time, angle=np.pi)\n"                                     
             << "\n"                                                                        
             << "def y(q, time):\n"                                                         
             << "    return ry(q, time, angle=np.pi)\n"                                     
             << "\n"                                                                        
             << "def rx90(q, time):\n"                                                      
             << "    return rx(q, time, angle=np.pi/2)\n"                                   
             << "\n"                                                                        
             << "def ry90(q, time):\n"                                                      
             << "    return ry(q, time, angle=np.pi/2)\n"                                   
             << "\n"                                                                        
             << "def xm90(q, time):\n"                                                      
             << "    return rx(q, time, angle=-np.pi/2)\n"                                  
             << "\n"                                                                        
             << "def ym90(q, time):\n"                                                      
             << "    return ry(q, time, angle=-np.pi/2)\n"                                  
             << "\n"                                                                        
             << "def rx45(q, time):\n"                                                      
             << "    return rx(q, time, angle=np.pi/4)\n"                                   
             << "\n"                                                                        
             << "def xm45(q, time):\n"                                                      
             << "    return rx(q, time, angle=-np.pi/4)\n"                                  
             << "\n"                                                                        
             << "def prepz(q, time):\n"                                                    
             << "    return ResetGate(q, time, state=0)\n\n"                                
             << endl;

        fout << "\n# create a circuit\n";
        fout << "c = Circuit(title=\"" << prog_name << "\")\n\n";

        DOUT("Adding qubits to Quantumsim program");
        fout << "\n# add qubits\n";
        json config;
        try
        {
            config = load_json(platform.configuration_file_name);
        }
        catch (json::exception e)
        {
            throw ql::exception("[x] error : ql::quantumsim_compiler::load() :  failed to load the hardware config file : malformed json file ! : \n\t"+
                                std::string(e.what()),false);
        }

        // load qubit attributes
        json qubit_attributes = config["qubit_attributes"];
        if (qubit_attributes.is_null())
        {
            EOUT("qubit_attributes is not specified in the hardware config file !");
            throw ql::exception("[x] error: quantumsim_compiler: qubit_attributes is not specified in the hardware config file !",false);
        }
        json relaxation_times = qubit_attributes["relaxation_times"];
        if (relaxation_times.is_null())
        {
            EOUT("relaxation_times is not specified in the hardware config file !");
            throw ql::exception("[x] error: quantumsim_compiler: relaxation_times is not specified in the hardware config file !",false);
        }
        size_t count =  platform.hardware_settings["qubit_number"];

        for (json::iterator it = relaxation_times.begin(); it != relaxation_times.end(); ++it)
        {
            size_t q = stoi(it.key());
            if (q >= count)
            {
                EOUT("qubit_attribute.relaxation_time.qubit number is not in qubits available in the platform");
                throw ql::exception("[x] error: qubit_attribute.relaxation_time.qubit number is not in qubits available in the platform",false);
            }
            auto & rt = it.value();
            if (rt.size() < 2)
            {
                EOUT("each qubit must have at least two relaxation times");
                throw ql::exception("[x] error: each qubit must have at least two relaxation times",false);
            }
            fout << "c.add_qubit(\"q" << q <<"\", " << rt[0] << ", " << rt[1] << ")\n" ;
        }

        DOUT("Adding Gates to Quantumsim program");
        fout << "\n# add gates\n";
        for(auto &kernel : kernels)
        {
            DOUT("... adding gates, a new kernel");
            if (kernel.bundles.empty())
            {
                IOUT("No bundles for adding gates");
            }
            else
            {
                for ( ql::ir::bundle_t & abundle : kernel.bundles)
                {
                    DOUT("... adding gates, a new bundle");
                    auto bcycle = abundle.start_cycle;
        
                    std::stringstream ssbundles;
                    for( auto secIt = abundle.parallel_sections.begin(); secIt != abundle.parallel_sections.end(); ++secIt )
                    {
                        DOUT("... adding gates, a new section in a bundle");
                        for(auto insIt = secIt->begin(); insIt != secIt->end(); ++insIt )
                        {
                            auto & iname = (*insIt)->name;
                            auto & operands = (*insIt)->operands;
                            if( iname == "measure")
                            {
                                DOUT("... adding gates, a measure");
                                auto op = operands.back();
                                ssbundles << "\nsampler = uniform_noisy_sampler(readout_error=0.03, seed=42)\n";
                                ssbundles << "c.add_qubit(\"m" << op <<"\")\n";
                                ssbundles << "c.add_measurement("
                                          << "\"q" << op <<"\", "
                                          << "time=" << bcycle << ", "
                                          << "output_bit=\"m" << op <<"\", "
                                          << "sampler=sampler"
                                          << ")\n" ;
                            }
                            else
                            {
                                DOUT("... adding gates, another gate");
                                ssbundles <<  "c.add_gate("<< iname << "(" ;
                                size_t noperands = operands.size();
                                if( noperands > 0 )
                                {
                                    for(auto opit = operands.begin(); opit != operands.end()-1; opit++ )
                                        ssbundles << "\"q" << *opit <<"\", ";
                                    ssbundles << "\"q" << operands.back()<<"\"";
                                }
                                ssbundles << ", time=" << bcycle << "))" << endl;
                            }
                        }
                    }
                    fout << ssbundles.str();
                }
                std::vector<size_t> usedcyclecount;
                kernel.get_qubit_usedcyclecount(usedcyclecount);
                fout << "# ----- depth: " << kernel.get_depth() << "\n";
                fout << "# ----- quantum gates: " << kernel.get_quantum_gates_count() << "\n";
                fout << "# ----- non single qubit gates: " << kernel.get_non_single_qubit_quantum_gates_count() << "\n";
                fout << "# ----- swaps added: " << kernel.swaps_added << "\n";
                fout << "# ----- moves added: " << kernel.moves_added << "\n";
                fout << "# ----- classical operations: " << kernel.get_classical_operations_count() << "\n";
                fout << "# ----- qubits used: " << kernel.get_qubit_usecount() << "\n";
                fout << "# ----- qubit cycles use: ";
                    int started = 0;
                    for (auto v : usedcyclecount)
                    {
                        if (started) { fout << ", "; } else { fout << "["; started = 1; }
                        fout << v;
                    }
                    fout << "]" << "\n";
            }
        }
        size_t total_depth = 0;
        size_t total_quantum_gates = 0;
        size_t total_classical_operations = 0;
        size_t total_non_single_qubit_gates= 0;
        size_t total_swaps = 0;
        size_t total_moves = 0;
        for(auto &kernel : kernels)
        {
            total_depth += kernel.get_depth();
            total_classical_operations += kernel.get_classical_operations_count();
            total_quantum_gates += kernel.get_quantum_gates_count();
            total_non_single_qubit_gates += kernel.get_non_single_qubit_quantum_gates_count();
            total_swaps += kernel.swaps_added;
            total_moves += kernel.moves_added;
        }
        fout << "\n";
        fout << "# Program-wide statistics:\n";
        fout << "# Total depth: " << total_depth << "\n";
        fout << "# Total no. of quantum gates: " << total_quantum_gates << "\n";
        fout << "# Total no. of non single qubit gates: " << total_non_single_qubit_gates << "\n";
        fout << "# Total no. of swaps: " << total_swaps << "\n";
        fout << "# Total no. of moves of swaps: " << total_moves << "\n";
        fout << "# Total no. of classical operations: " << total_classical_operations << "\n";
        fout << "# Qubits used: " << get_qubit_usecount(kernels) << "\n";
        fout << "# No. kernels: " << kernels.size() << "\n";

        fout.close();
        IOUT("Writing scheduled Quantumsim program [Done]");
    }
};

} // arch
} // ql

#endif // QL_QUANTUMSIM_EQASM_COMPILER_H


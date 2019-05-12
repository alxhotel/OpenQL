/**
 * @file   scheduler.h
 * @date   01/2017
 * @author Imran Ashraf
 * @author Hans van Someren
 * @brief  ASAP/ALAP critical path and UNIFORM scheduling with and without resource constraint
 */

#ifndef _SCHEDULER_H
#define _SCHEDULER_H

/*
    Summary

    Below there really are two classes: the dependence graph definition and the scheduler definition.
    All schedulers require dependence graph creation as preprocessor, and don't modify it.
    For each kernel's circuit a private dependence graph is created.
    The schedulers modify the order of gates in the circuit, initialize the cycle field of each gate,
    and generate/return the bundles, a list of bundles in which gates starting in the same cycle are grouped.

    The dependence graph (represented by the graph field below) is created in the Init method,
    and the graph is constructed from and referring to the gates in the sequence of gates in the kernel's circuit.
    In this graph, the nodes refer to the gates in the circuit, and the edges represent the dependences between two gates.
    Init scans the gates of the circuit from start to end, inspects their parameters, and for each gate
    depending on the gate type and parameter value and previous gates operating on the same parameters,
    it creates a dependence of the current gate on that previous gate.
    Such a dependence has a type (RAW, WAW, etc.), cause (the qubit or classical register used as parameter), and a weight
    (the cycles the previous gate takes to complete its execution, after which the current gate can start execution).

    In dependence graph creation, each qubit/classical register (creg) use in each gate is seen as an "event".
    The following events are distinguished:
    - W for Write: such a use must sequentialize with any previous and later uses of the same qubit/creg.
        This is the default for qubits in a gate and for assignment/modifications in classical code.
    - R for Read: such uses can be arbitrarily reordered (as long as other dependences allow that).
        This event applies to all operands of CZ, the first operand of CNOT gates, and to all reads in classical code.
        It also applies in general to the control operand of Control Unitaries.
        It represents commutativity between the gates which such use: CU(a,b), CZ(a,c), CZ(d,a) and CNOT(a,e) all commute.
    - D: such uses can be arbitrarily reordered but are sequentialized with W and R events on the same qubit.
        This event applies to the second operand of CNOT gates: CNOT(a,d) and CNOT(b,d) commute.
    With this, we effectively get the following table of event transitions (from left-bottom to right-up),
    in which 'no' indicates no dependence from left event to top event and '/' indicates a dependence from left to top.
             W   R   D                  w   R   D
        W    /   /   /              W   WAW RAW DAW
        R    /   no  /              R   WAR RAR DAR
        D    /   /   no             D   WAD RAD DAD
    When the 'no' dependences are created (RAR and/or DAD),
    the respective commutatable gates are sequentialized according to the original circuit's order.
    With all 'no's replaced by '/', all event types become equivalent (i.e. as if they were Write).

    Schedulers come essentially in the following forms:
    - ASAP: a plain forward scheduler using dependences only, aiming at execution each gate as soon as possible
    - ASAP with resource constraints: similar but taking resource constraints of the gates of the platform into account
    - ALAP: as ASAP but then aiming at execution of each gate as late as possible
    - ALAP with resource constraints: similar but taking resource constraints of the gates of the platform into account
    - ALAP with UNIFORM bundle lengths: using dependences only, aim at ALAP but with equally length bundles
    ASAP/ALAP can be controlled by the "scheduler" option. Similarly for UNIFORM ("scheduler_uniform").
    With/out resource constraints are separate method calls.
    The current code implements behavior before and after solving issue 179, selectable by an option ("scheduler_post179").
    The post179 behavior supports commutation and in general produces more efficient/shorter scheduled circuits;
    that the scheduler commutes gates when possible is enabled by default and can be controlled by option "scheduler_commute".
 */

#include <lemon/list_graph.h>
#include <lemon/lgf_reader.h>
#include <lemon/lgf_writer.h>
#include <lemon/dijkstra.h>
#include <lemon/connectivity.h>

#include "ql/utils.h"
#include "ql/gate.h"
#include "ql/circuit.h"
#include "ql/ir.h"
#include "ql/resource_manager.h"

using namespace std;
using namespace lemon;

// see above/below for the meaning of R, W, and D events and their relation to dependences
enum DepTypes{RAW, WAW, WAR, RAR, RAD, DAR, DAD, WAD, DAW};
const string DepTypesNames[] = {"RAW", "WAW", "WAR", "RAR", "RAD", "DAR", "DAD", "WAD", "DAW"};

class Scheduler
{
public:
    // dependence graph is constructed (see Init) once from the sequence of gates in a kernel's circuit
    // it can be reused as often as needed as long as no gates are added/deleted; it doesn't modify those gates
    ListDigraph graph;

    // conversion between gate* (pointer to the gate in the circuit) and node (of the dependence graph)
    ListDigraph::NodeMap<ql::gate*> instruction;// instruction[n] == gate*
    std::map<ql::gate*,ListDigraph::Node>  node;// node[gate*] == n

    // attributes
    ListDigraph::NodeMap<std::string> name;     // name[n] == qasm string
    ListDigraph::ArcMap<int> weight;            // number of cycles of dependence
    ListDigraph::ArcMap<int> cause;             // qubit/creg index of dependence
    ListDigraph::ArcMap<int> depType;           // RAW, WAW, ...

    // s and t nodes are the top and bottom of the dependence graph
    ListDigraph::Node s, t;                     // instruction[s]==SOURCE, instruction[t]==SINK

    // parameters of dependence graph construction
    size_t          cycle_time;                 // to convert durations to cycles as weight of dependence
    size_t          qubit_count;                // number of qubits, to check/represent qubit as cause of dependence
    size_t          creg_count;                 // number of cregs, to check/represent creg as cause of dependence
    ql::circuit*    circp;                      // current and result circuit, passed from Init to each scheduler

    // scheduler support
    std::map< std::pair<std::string,std::string>, size_t> buffer_cycles_map;
    std::map<ListDigraph::Node,size_t>  remaining;  // remaining[node] == cycles until end; critical path representation


public:
    Scheduler(): instruction(graph), name(graph), weight(graph),
        cause(graph), depType(graph) {}

    // factored out code from Init to add a dependence between two nodes
    void addDep(int srcID, int tgtID, enum DepTypes deptype, int operand)
    {
        ListDigraph::Node srcNode = graph.nodeFromId(srcID);
        ListDigraph::Node tgtNode = graph.nodeFromId(tgtID);
        ListDigraph::Arc arc = graph.addArc(srcNode,tgtNode);
        weight[arc] = std::ceil( static_cast<float>(instruction[srcNode]->duration) / cycle_time);
        cause[arc] = operand;
        depType[arc] = deptype;
        // DOUT("... dep " << name[srcNode] << " -> " << name[tgtNode] << " (opnd=" << operand << ", dep=" << DepTypesNames[deptype] << ")");
    }

    // fill the dependence graph ('graph') with nodes from the circuit and adding arcs for their dependences
    void Init(ql::circuit& ckt, ql::quantum_platform platform, size_t qcount, size_t ccount)
    {
        DOUT("Dependence graph creation ...");
        qubit_count = qcount;
        creg_count = ccount;
        size_t qubit_creg_count = qubit_count + creg_count;
        cycle_time = platform.cycle_time;
        circp = &ckt;

        // populate buffer map
        // 'none' type is a dummy type and 0 buffer cycles will be inserted for
        // instructions of type 'none'
        //
        // this has nothing to do with dependence graph generation but with scheduling
        // so should be in resource-constrained scheduler constructor
        std::vector<std::string> buffer_names = {"none", "mw", "flux", "readout"};
        for(auto & buf1 : buffer_names)
        {
            for(auto & buf2 : buffer_names)
            {
                auto bpair = std::pair<std::string,std::string>(buf1,buf2);
                auto bname = buf1+ "_" + buf2 + "_buffer";
                if(platform.hardware_settings.count(bname) > 0)
                {
                    buffer_cycles_map[ bpair ] = std::ceil( 
                        static_cast<float>(platform.hardware_settings[bname]) / cycle_time);
                }
                // DOUT("Initializing " << bname << ": "<< buffer_cycles_map[bpair]);
            }
        }

        // dependences are created with a current gate as target
        // and with those previous gates as source that have an operand match:
        // - the previous gates that Read r in LastReaders[r]; this is a list
        // - the previous gates that D qubit q in LastDs[q]; this is a list
        // - the previous gate that Wrote r in LastWriter[r]; this can only be one
        // operands can be a qubit or a classical register
        typedef vector<int> ReadersListType;

        vector<ReadersListType> LastReaders;
        LastReaders.resize(qubit_creg_count);

        vector<ReadersListType> LastDs;
        LastDs.resize(qubit_creg_count);

        // start filling the dependence graph by creating the s node, the top of the graph
        {
            // add dummy source node
            ListDigraph::Node srcNode = graph.addNode();
            instruction[srcNode] = new ql::SOURCE();
            node[instruction[srcNode]] = srcNode;
            name[srcNode] = instruction[srcNode]->qasm();
            s=srcNode;
        }
        int srcID = graph.id(s);
        vector<int> LastWriter(qubit_creg_count,srcID);     // it implicitly writes to all qubits and class. regs

        // for each gate pointer ins in the circuit, add a node and add dependences from previous gates to it
        for( auto ins : ckt )
        {
            DOUT("Current instruction : " << ins->qasm());

            // Add node
            ListDigraph::Node consNode = graph.addNode();
            int consID = graph.id(consNode);
            instruction[consNode] = ins;
            node[ins] = consNode;
            name[consNode] = ins->qasm();

            // Add edges (arcs)
            // In quantum computing there are no real Reads and Writes on qubits because they cannot be cloned.
            // Every qubit use influences the qubit, updates it, so would be considered a Read+Write at the same time.
            // In dependence graph construction, this leads to WAW-dependence chains of all uses of the same qubit,
            // and hence in a scheduler using this graph to a sequentialization of those uses in the original program order.
            //
            // For a scheduler, only the presence of a dependence counts, not its type (RAW/WAW/etc.).
            // A dependence graph also has other uses apart from the scheduler: e.g. to find chains of live qubits,
            // from their creation (Prep etc.) to their destruction (Measure, etc.) in allocation of virtual to real qubits.
            // For those uses it makes sense to make a difference with a gate doing a Read+Write, just a Write or just a Read:
            // a Prep creates a new 'value' (Write); wait, display, x, swap, cnot, all pass this value on (so Read+Write),
            // while a Measure 'destroys' the 'value' (Read+Write of the qubit, Write of the creg),
            // the destruction aspect of a Measure being implied by it being followed by a Prep (Write only) on the same qubit.
            // Furthermore Writes can model barriers on a qubit (see Wait, Display, etc.), because Writes sequentialize.
            // The dependence graph creation below models a graph suitable for all functions, including chains of live qubits.

            if (ql::options::get("scheduler_post179") == "yes")
            {
            // Control-operands of Controlled Unitaries commute, independent of the Unitary,
            // i.e. these gates need not be kept in order.
            // But, of course, those qubit uses should be ordered after (/before) the last (/next) non-control use of the qubit.
            // In this way, those control-operand qubit uses would be like pure Reads in dependence graph construction.
            // A problem might be that the gates with the same control-operands might be scheduled in parallel then.
            // In a non-resource scheduler that will happen but it doesn't do harm because it is not a real machine.
            // In a resource-constrained scheduler the resource constraint that prohibits more than one use
            // of the same qubit being active at the same time, will prevent this parallelism.
            // So ignoring Read After Read (RAR) dependences enables the scheduler to take advantage
            // of the commutation property of Controlled Unitaries without disadvantages.
            //
            // In more detail:
            // 1. CU1(a,b) and CU2(a,c) commute (for any U1, U2, so also can be equal and/or be CNOT and/or be CZ)
            // 2. CNOT(a,b) and CNOT(c,b) commute (property of CNOT only).
            // 3. CZ(a,b) and CZ(b,a) are identical (property of CZ only).
            // 4. CNOT(a,b) commutes with CZ(a,c) (from 1.) and thus with CZ(c,a) (from 3.)
            // 5. CNOT(a,b) does not commute with CZ(c,b) (and thus not with CZ(b,c), from 3.)
            // To support this, next to R and W a D (for controlleD operand :-) is introduced for the target operand of CNOT.
            // The events (instead of just Read and Write) become then:
            // - Both operands of CZ are just Read.
            // - The control operand of CNOT is Read, the target operand is D.
            // - Of any other Control Unitary, the control operand is Read and the target operand is Write (not D!)
            // - Of any other gate the operands are Read+Write or just Write (as usual to represent flow).
            // With this, we effectively get the following table of event transitions (from left-bottom to right-up),
            // in which 'no' indicates no dependence from left event to top event and '/' indicates a dependence from left to top.
            //
            //             W   R   D                  w   R   D
            //        W    /   /   /              W   WAW RAW DAW
            //        R    /   no  /              R   WAR RAR DAR
            //        D    /   /   no             D   WAD RAD DAD
            //
            // In addition to LastReaders, we introduce LastDs.
            // Either one is cleared when dependences are generated from them, and extended otherwise.
            // From the table it can be seen that the D 'behaves' as a Write to Read, and as a Read to Write,
            // that there is no order among Ds nor among Rs, but D after R and R after D sequentialize.
            // With this, the dependence graph is claimed to represent the commutations as above.
            //
            // The post179 schedulers are list schedulers, i.e. they maintain a list of gates in their algorithm,
            // of gates available for being scheduled because they are not blocked by dependences on non-scheduled gates.
            // Therefore, the post179 schedulers are able to select the best one from a set of commutable gates.
            }

            // each type of gate has a different 'signature' of events; switch out to each one
            if(ins->name == "measure")
            {
                // DOUT(". considering " << name[consNode] << " as measure");
                // Read+Write each qubit operand + Write corresponding creg
                auto operands = ins->operands;
                for( auto operand : operands )
                {
                    // DOUT(".. Operand: " << operand);
                    addDep(LastWriter[operand], consID, WAW, operand);
                    for(auto & readerID : LastReaders[operand])
                    {
                        addDep(readerID, consID, WAR, operand);
                    }
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        for(auto & readerID : LastDs[operand])
                        {
                            addDep(readerID, consID, WAD, operand);
                        }
                    }
                }

                ql::measure * mins = (ql::measure*)ins;
                for( auto operand : mins->creg_operands )
                {
                    // DOUT(".. Operand: " << operand);
                    addDep(LastWriter[qubit_count+operand], consID, WAW, operand);
                    for(auto & readerID : LastReaders[qubit_count+operand])
                    {
                        addDep(readerID, consID, WAR, operand);
                    }
                }

                // update LastWriter and so clear LastReaders
                for( auto operand : operands )
                {
                    LastWriter[operand] = consID;
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        LastReaders[operand].clear();
                        LastDs[operand].clear();
                    }
                }
                for( auto operand : mins->creg_operands )
                {
                    LastWriter[operand] = consID;
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        LastReaders[operand].clear();
                    }
                }
            }
            else if(ins->name == "display")
            {
                // DOUT(". considering " << name[consNode] << " as display");
                // no operands, display all qubits and cregs
                // Read+Write each operand
                std::vector<size_t> qubits(qubit_creg_count);
                std::iota(qubits.begin(), qubits.end(), 0);
                for( auto operand : qubits )
                {
                    // DOUT(".. Operand: " << operand);
                    addDep(LastWriter[operand], consID, WAW, operand);
                    for(auto & readerID : LastReaders[operand])
                    {
                        addDep(readerID, consID, WAR, operand);
                    }
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        for(auto & readerID : LastDs[operand])
                        {
                            addDep(readerID, consID, WAD, operand);
                        }
                    }
                }

                // now update LastWriter and so clear LastReaders/LastDs
                for( auto operand : qubits )
                {
                    LastWriter[operand] = consID;
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        LastReaders[operand].clear();
                        LastDs[operand].clear();
                    }
                }
            }
            else if(ins->type() == ql::gate_type_t::__classical_gate__)
            {
                // DOUT(". considering " << name[consNode] << " as classical gate");
                std::vector<size_t> all_operands(qubit_creg_count);
                std::iota(all_operands.begin(), all_operands.end(), 0);
                for( auto operand : all_operands )
                {
                    // DOUT(".. Operand: " << operand);
                    addDep(LastWriter[operand], consID, WAW, operand);
                    for(auto & readerID : LastReaders[operand])
                    {
                        addDep(readerID, consID, WAR, operand);
                    }
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        for(auto & readerID : LastDs[operand])
                        {
                            addDep(readerID, consID, WAD, operand);
                        }
                    }
                }

                // now update LastWriter and so clear LastReaders/LastDs
                for( auto operand : all_operands )
                {
                    LastWriter[operand] = consID;
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        LastReaders[operand].clear();
                        LastDs[operand].clear();
                    }
                }
            }
            else if (  ins->name == "cnot"
                    )
            {
                // DOUT(". considering " << name[consNode] << " as cnot");
                // CNOTs Read the first operands, and Ds the second operand
                size_t operandNo=0;
                auto operands = ins->operands;
                for( auto operand : operands )
                {
                    // DOUT(".. Operand: " << operand);
                    if( operandNo == 0)
                    {
                        addDep(LastWriter[operand], consID, RAW, operand);
	                    if (ql::options::get("scheduler_post179") == "no"
	                    ||  ql::options::get("scheduler_commute") == "no")
                        {
                            for(auto & readerID : LastReaders[operand])
                            {
                                addDep(readerID, consID, RAR, operand);
                            }
                        }
                        if (ql::options::get("scheduler_post179") == "yes")
                        {
                            for(auto & readerID : LastDs[operand])
                            {
                                addDep(readerID, consID, RAD, operand);
                            }
                        }
                    }
                    else
                    {
	                    if (ql::options::get("scheduler_post179") == "no")
                        {
                            addDep(LastWriter[operand], consID, WAW, operand);
                            for(auto & readerID : LastReaders[operand])
                            {
                                addDep(readerID, consID, WAR, operand);
                            }
                        }
                        else
                        {
                            addDep(LastWriter[operand], consID, DAW, operand);
	                        if (ql::options::get("scheduler_commute") == "no")
                            {
                                for(auto & readerID : LastDs[operand])
                                {
                                    addDep(readerID, consID, DAD, operand);
                                }
                            }
                            for(auto & readerID : LastReaders[operand])
                            {
                                addDep(readerID, consID, DAR, operand);
                            }
                        }
                    }
                    operandNo++;
                } // end of operand for

                // now update LastWriter and so clear LastReaders
                operandNo=0;
                for( auto operand : operands )
                {
                    if( operandNo == 0)
                    {
                        // update LastReaders for this operand 0
                        LastReaders[operand].push_back(consID);
                        if (ql::options::get("scheduler_post179") == "yes")
                        {
                            LastDs[operand].clear();
                        }
                    }
                    else
                    {
	                    if (ql::options::get("scheduler_post179") == "no")
                        {
	                        LastWriter[operand] = consID;
                        }
                        else
                        {
                            LastDs[operand].push_back(consID);
                        }
	                    LastReaders[operand].clear();
                    }
                    operandNo++;
                }
            }
            else if (  ins->name == "cz"
                    || ins->name == "cphase"
                    )
            {
                // DOUT(". considering " << name[consNode] << " as cz");
                // CZs Read all operands for post179
                // CZs Read all operands and write last one for pre179 
                size_t operandNo=0;
                auto operands = ins->operands;
                for( auto operand : operands )
                {
                    // DOUT(".. Operand: " << operand);
                    if (ql::options::get("scheduler_post179") == "no")
                    {
                        addDep(LastWriter[operand], consID, RAW, operand);
                        for(auto & readerID : LastReaders[operand])
                        {
                            addDep(readerID, consID, RAR, operand);
                        }
	                    if( operandNo != 0)
	                    {
                            addDep(LastWriter[operand], consID, WAW, operand);
                            for(auto & readerID : LastReaders[operand])
                            {
                                addDep(readerID, consID, WAR, operand);
                            }
	                    }
                    }
                    else
                    {
                        if (ql::options::get("scheduler_commute") == "no")
                        {
                            for(auto & readerID : LastReaders[operand])
                            {
                                addDep(readerID, consID, RAR, operand);
                            }
                        }
                        addDep(LastWriter[operand], consID, RAW, operand);
                        for(auto & readerID : LastDs[operand])
                        {
                            addDep(readerID, consID, RAD, operand);
                        }
                    }
                    operandNo++;
                } // end of operand for

                // update LastReaders etc.
                operandNo=0;
                for( auto operand : operands )
                {
                    if (ql::options::get("scheduler_post179") == "no")
                    {
	                    if( operandNo == 0)
	                    {
	                        LastReaders[operand].push_back(consID);
	                    }
	                    else
	                    {
	                        LastWriter[operand] = consID;
	                        LastReaders[operand].clear();
	                    }
                    }
                    else
                    {
                        LastDs[operand].clear();
                        LastReaders[operand].push_back(consID);
                    }
                    operandNo++;
                }
            }
#ifdef HAVEGENERALCONTROLUNITARIES
            else if (
                    // or is a Control Unitary in general
                    // Read on all operands, Write on last operand
                    // before implementing it, check whether all commutativity on Reads above hold for this Control Unitary
                    )
            {
                // DOUT(". considering " << name[consNode] << " as Control Unitary");
                // Control Unitaries Read all operands, and Write the last operand
                size_t operandNo=0;
                auto operands = ins->operands;
                size_t op_count = operands.size();
                for( auto operand : operands )
                {
                    // DOUT(".. Operand: " << operand);
                    addDep(LastWriter[operand], consID, RAW, operand);
                    if (ql::options::get("scheduler_post179") == "no"
                    ||  ql::options::get("scheduler_commute") == "no")
                    {
                        for(auto & readerID : LastReaders[operand])
                        {
                            addDep(readerID, consID, RAR, operand);
                        }
                    }
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        for(auto & readerID : LastDs[operand])
                        {
                            addDep(readerID, consID, RAD, operand);
                        }
                    }

                    if( operandNo < op_count-1 )
                    {
                        LastReaders[operand].push_back(consID);
                        if (ql::options::get("scheduler_post179") == "yes")
                        {
                            LastDs[operand].clear();
                        }
                    }
                    else
                    {
                        addDep(LastWriter[operand], consID, WAW, operand);
                        for(auto & readerID : LastReaders[operand])
                        {
                            addDep(readerID, consID, WAR, operand);
                        }
                        if (ql::options::get("scheduler_post179") == "yes")
                        {
                            for(auto & readerID : LastDs[operand])
                            {
                                addDep(readerID, consID, WAD, operand);
                            }
                        }

                        LastWriter[operand] = consID;
                        LastReaders[operand].clear();
                        if (ql::options::get("scheduler_post179") == "yes")
                        {
                            LastDs[operand].clear();
                        }
                    }
                    operandNo++;
                } // end of operand for
            }
#endif  // HAVEGENERALCONTROLUNITARIES
            else
            {
                // DOUT(". considering " << name[consNode] << " as general quantum gate");
                // general quantum gate, Read+Write on each operand
                size_t operandNo=0;
                auto operands = ins->operands;
                for( auto operand : operands )
                {
                    // DOUT(".. Operand: " << operand);
                    addDep(LastWriter[operand], consID, WAW, operand);
                    for(auto & readerID : LastReaders[operand])
                    {
                        addDep(readerID, consID, WAR, operand);
                    }
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        for(auto & readerID : LastDs[operand])
                        {
                            addDep(readerID, consID, WAD, operand);
                        }
                    }

                    LastWriter[operand] = consID;
                    LastReaders[operand].clear();
                    if (ql::options::get("scheduler_post179") == "yes")
                    {
                        LastDs[operand].clear();
                    }
                    
                    operandNo++;
                } // end of operand for
            } // end of if/else
        } // end of instruction for

        // finish filling the dependence graph by creating the t node, the bottom of the graph
        {
	        // add dummy target node
	        ListDigraph::Node consNode = graph.addNode();
	        int consID = graph.id(consNode);
	        instruction[consNode] = new ql::SINK();
	        node[instruction[consNode]] = consNode;
	        name[consNode] = instruction[consNode]->qasm();
	        t=consNode;
	
	        DOUT("adding deps to SINK");
	        // add deps to the dummy target node to close the dependence chains
	        // it behaves as a W to every qubit and creg
	        //
	        // to guarantee that exactly at start of execution of dummy SINK,
	        // all still executing nodes complete, give arc weight of those nodes;
	        // this is relevant for ALAP (which starts backward from SINK for all these nodes);
	        // also for accurately computing the circuit's depth (which includes full completion);
	        // and also for implementing scheduling and mapping across control-flow (so that it is
	        // guaranteed that on a jump and on start of target circuit, the source circuit completed).
            //
            // note that there always is a LastWriter: the dummy source node wrote to every qubit and class. reg
	        std::vector<size_t> qubits(qubit_creg_count);
	        std::iota(qubits.begin(), qubits.end(), 0);
	        for( auto operand : qubits )
	        {
	            // DOUT(".. Operand: " << operand);
	            addDep(LastWriter[operand], consID, WAW, operand);
	            for(auto & readerID : LastReaders[operand])
	            {
	                addDep(readerID, consID, WAR, operand);
	            }
	            if (ql::options::get("scheduler_post179") == "yes")
	            {
	                for(auto & readerID : LastDs[operand])
	                {
	                    addDep(readerID, consID, WAD, operand);
	                }
	            }
	        }
	
            // useless because there is nothing after t but destruction
	        for( auto operand : qubits )
	        {
	            LastWriter[operand] = consID;
	            LastReaders[operand].clear();
	            if (ql::options::get("scheduler_post179") == "yes")
	            {
	                LastDs[operand].clear();
	            }
	        }
        }

        // useless as well because by construction, there cannot be cycles
        // but when afterwards dependences are added, cycles may be created,
        // and after doing so (a copy of) this test should certainly be done because
        // a cyclic dependence graph cannot be scheduled;
        // this test here is a kind of debugging aid whether dependence creation was done well
        if( !dag(graph) )
        {
            DOUT("The dependence graph is not a DAG.");
            EOUT("The dependence graph is not a DAG.");
        }
        DOUT("Dependence graph creation Done.");
    }

    void Print()
    {
        COUT("Printing Dependence Graph ");
        digraphWriter(graph).
        nodeMap("name", name).
        arcMap("cause", cause).
        arcMap("weight", weight).
        // arcMap("depType", depType).
        node("source", s).
        node("target", t).
        run();
    }

    void PrintMatrix()
    {
        COUT("Printing Dependence Graph as Matrix");
        ofstream fout;
        string datfname( ql::options::get("output_dir") + "/dependenceMatrix.dat");
        fout.open( datfname, ios::binary);
        if ( fout.fail() )
        {
            EOUT("opening file " << datfname << std::endl
                     << "Make sure the output directory ("<< ql::options::get("output_dir") << ") exists");
            return;
        }

        size_t totalInstructions = countNodes(graph);
        vector< vector<bool> > Matrix(totalInstructions, vector<bool>(totalInstructions));

        // now print the edges
        for (ListDigraph::ArcIt arc(graph); arc != INVALID; ++arc)
        {
            ListDigraph::Node srcNode = graph.source(arc);
            ListDigraph::Node dstNode = graph.target(arc);
            size_t srcID = graph.id( srcNode );
            size_t dstID = graph.id( dstNode );
            Matrix[srcID][dstID] = true;
        }

        for(size_t i=1; i<totalInstructions-1;i++)
        {
            for(size_t j=1; j<totalInstructions-1;j++)
            {
                fout << Matrix[j][i] << "\t";
            }
            fout << endl;
        }

        fout.close();
    }

    // void PrintDot1_(
    //             bool WithCritical,
    //             bool WithCycles,
    //             ListDigraph::NodeMap<size_t> & cycle,
    //             std::vector<ListDigraph::Node> & order,
    //             std::ostream& dotout
    //             )
    // {
    //     ListDigraph::ArcMap<bool> isInCritical(graph);
    //     if(WithCritical)
    //     {
    //         for (ListDigraph::ArcIt a(graph); a != INVALID; ++a)
    //         {
    //             isInCritical[a] = false;
    //             for ( Path<ListDigraph>::ArcIt ap(p); ap != INVALID; ++ap )
    //             {
    //                 if(a==ap)
    //                 {
    //                     isInCritical[a] = true;
    //                     break;
    //                 }
    //             }
    //         }
    //     }

    //     string NodeStyle(" fontcolor=black, style=filled, fontsize=16");
    //     string EdgeStyle1(" color=black");
    //     string EdgeStyle2(" color=red");
    //     string EdgeStyle = EdgeStyle1;

    //     dotout << "digraph {\ngraph [ rankdir=TD; ]; // or rankdir=LR"
    //         << "\nedge [fontsize=16, arrowhead=vee, arrowsize=0.5];"
    //         << endl;

    //     // first print the nodes
    //     for (ListDigraph::NodeIt n(graph); n != INVALID; ++n)
    //     {
    //         int nid = graph.id(n);
    //         string nodeName = name[n];
    //         dotout  << "\"" << nid << "\""
    //                 << " [label=\" " << nodeName <<" \""
    //                 << NodeStyle
    //                 << "];" << endl;
    //     }

    //     if( WithCycles)
    //     {
    //         // Print cycle numbers as timeline, as shown below
    //         size_t cn=0,TotalCycles=0;
    //         dotout << "{\nnode [shape=plaintext, fontsize=16, fontcolor=blue]; \n";
    //         ListDigraph::NodeMap<size_t>::MapIt it(cycle);
    //         if(it != INVALID)
    //             TotalCycles=cycle[it];
    //         for(cn=0;cn<=TotalCycles;++cn)
    //         {
    //             if(cn>0)
    //                 dotout << " -> ";
    //             dotout << "Cycle" << cn;
    //         }
    //         dotout << ";\n}\n";

    //         // Now print ranks, as shown below
    //         std::vector<ListDigraph::Node>::reverse_iterator rit;
    //         for ( rit = order.rbegin(); rit != order.rend(); ++rit)
    //         {
    //             int nid = graph.id(*rit);
    //             dotout << "{ rank=same; Cycle" << cycle[*rit] <<"; " <<nid<<"; }\n";
    //         }
    //     }

    //     // now print the edges
    //     for (ListDigraph::ArcIt arc(graph); arc != INVALID; ++arc)
    //     {
    //         ListDigraph::Node srcNode = graph.source(arc);
    //         ListDigraph::Node dstNode = graph.target(arc);
    //         int srcID = graph.id( srcNode );
    //         int dstID = graph.id( dstNode );

    //         if(WithCritical)
    //             EdgeStyle = ( isInCritical[arc]==true ) ? EdgeStyle2 : EdgeStyle1;

    //         dotout << dec
    //             << "\"" << srcID << "\""
    //             << "->"
    //             << "\"" << dstID << "\""
    //             << "[ label=\""
    //             << "q" << cause[arc]
    //             << " , " << weight[arc]
    //             << " , " << DepTypesNames[ depType[arc] ]
    //             <<"\""
    //             << " " << EdgeStyle << " "
    //             << "]"
    //             << endl;
    //     }

    //     dotout << "}" << endl;
    // }

    // void PrintDot()
    // {
    //     IOUT("Printing Dependence Graph in DOT");
    //     ofstream dotout;
    //     string dotfname(ql::options::get("output_dir") + "/dependenceGraph.dot");
    //     dotout.open(dotfname, ios::binary);
    //     if ( dotout.fail() )
    //     {
    //         EOUT("opening file " << dotfname << std::endl
    //                  << "Make sure the output directory ("<< ql::options::get("output_dir") << ") exists");
    //         return;
    //     }

    //     ListDigraph::NodeMap<size_t> cycle(graph);
    //     std::vector<ListDigraph::Node> order;
    //     PrintDot1_(false, false, cycle, order, dotout);
    //     dotout.close();
    // }

private:

// =========== pre179 schedulers

    void TopologicalSort(std::vector<ListDigraph::Node> & order)
    {
        // DOUT("Performing Topological sort.");
        ListDigraph::NodeMap<int> rorder(graph);
        if( !dag(graph) )
        {
            EOUT("This digraph is not a DAG.");
        }

        // result is in reverse topological order!
        topologicalSort(graph, rorder);

#ifdef DEBUG
        for (ListDigraph::ArcIt a(graph); a != INVALID; ++a)
        {
            if( rorder[graph.source(a)] > rorder[graph.target(a)] )
                EOUT("Wrong topologicalSort()");
        }
#endif

        for (ListDigraph::NodeMap<int>::MapIt it(rorder); it != INVALID; ++it)
        {
            order.push_back(it);
        }

        // DOUT("Nodes in Topological order:");
        // for ( std::vector<ListDigraph::Node>::reverse_iterator it = order.rbegin(); it != order.rend(); ++it)
        // {
        //     std::cout << name[*it] << std::endl;
        // }
    }

    void PrintTopologicalOrder()
    {
        std::vector<ListDigraph::Node> order;
        TopologicalSort(order);

        COUT("Printing nodes in Topological order");
        for ( std::vector<ListDigraph::Node>::reverse_iterator it = order.rbegin(); it != order.rend(); ++it)
        {
            std::cout << name[*it] << std::endl;
        }
    }

// =========== pre179 asap

    void schedule_asap_(ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order)
    {
        DOUT("Performing ASAP Scheduling");
        TopologicalSort(order);

        std::vector<ListDigraph::Node>::reverse_iterator currNode = order.rbegin();
        cycle[*currNode]=0; // src dummy in cycle 0
        ++currNode;
        while(currNode != order.rend() )
        {
            size_t currCycle=0;
            // DOUT("Scheduling " << name[*currNode]);
            for( ListDigraph::InArcIt arc(graph,*currNode); arc != INVALID; ++arc )
            {
                ListDigraph::Node srcNode  = graph.source(arc);
                size_t srcCycle = cycle[srcNode];
                if(currCycle < (srcCycle + weight[arc]))
                {
                    currCycle = srcCycle + weight[arc];
                }
            }
            cycle[*currNode]=currCycle;
            ++currNode;
        }

        DOUT("Performing ASAP Scheduling [Done].");
    }

    // with rc and latency compensation
    void schedule_asap_( ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order,
                       ql::arch::resource_manager_t & rm, const ql::quantum_platform & platform)
    {
        DOUT("Performing RC ASAP Scheduling");
        TopologicalSort(order);

        std::vector<ListDigraph::Node>::reverse_iterator currNode = order.rbegin();
        size_t currCycle=0;
        cycle[*currNode]=currCycle; // source node
        ++currNode;
        while(currNode != order.rend() )
        {
            DOUT("");
            auto & curr_ins = instruction[*currNode];
            auto & id = curr_ins->name;
            COUT("id: " << id);

            size_t op_start_cycle=0;
            DOUT("Scheduling " << name[*currNode]);
            for( ListDigraph::InArcIt arc(graph,*currNode); arc != INVALID; ++arc )
            {
                ListDigraph::Node srcNode  = graph.source(arc);
                size_t srcCycle = cycle[srcNode];
                if(op_start_cycle < (srcCycle + weight[arc]))
                {
                    op_start_cycle = srcCycle + weight[arc];
                }
            }

            if(curr_ins->type() == ql::gate_type_t::__dummy_gate__ ||
               curr_ins->type() == ql::gate_type_t::__classical_gate__)
            {
                cycle[*currNode]=op_start_cycle;
            }
            else
            {
                std::string operation_name(id);
                std::string operation_type; // MW/FLUX/READOUT etc
                std::string instruction_type; // single / two qubit
                size_t operation_duration = std::ceil( static_cast<float>(curr_ins->duration) / cycle_time);

                if(platform.instruction_settings.count(id) > 0)
                {
                    COUT("New count logic, Found " << id);
                    if(platform.instruction_settings[id].count("cc_light_instr") > 0)
                    {
                        operation_name = platform.instruction_settings[id]["cc_light_instr"];
                    }
                    if(platform.instruction_settings[id].count("type") > 0)
                    {
                        operation_type = platform.instruction_settings[id]["type"];
                    }
                    if(platform.instruction_settings[id].count("cc_light_instr_type") > 0)
                    {
                        instruction_type = platform.instruction_settings[id]["cc_light_instr_type"];
                    }
                }

                while(op_start_cycle < MAX_CYCLE)
                {
                    DOUT("Trying to schedule: " << name[*currNode] << "  in cycle: " << op_start_cycle);
                    DOUT("current operation_duration: " << operation_duration);
                    
                    try {
                        if( rm.available(op_start_cycle, curr_ins, operation_name, operation_type, instruction_type, operation_duration) )
                        {
                            DOUT("Resources available at cycle " << op_start_cycle << ", Scheduled.");

                            rm.reserve(op_start_cycle, curr_ins, operation_name, operation_type, instruction_type, operation_duration);
                            cycle[*currNode]=op_start_cycle;
                            break;
                        }
                        else
                        {
                            DOUT("Resources not available at cycle " << op_start_cycle << ", trying again ...");
                            ++op_start_cycle;
                        }
                    }
                    catch (std::runtime_error e)
                    {
                        // Add instruction to the end of list
                        
                        // Go to the next one
                        
                    }
                }

                if(op_start_cycle >= MAX_CYCLE)
                {
                    EOUT("Error: could not find schedule");
                    throw ql::exception("[x] Error : could not find schedule !",false);
                }
            }
            ++currNode;
        }

        // DOUT("Printing ASAP Schedule before latency compensation");
        // DOUT("Cycle    Instruction");
        // for ( auto it = order.rbegin(); it != order.rend(); ++it)
        // {
        //     DOUT( cycle[*it] << "  <- " << name[*it] );
        // }

        // latency compensation
        DOUT("Latency compensation ...");
        for ( auto it = order.begin(); it != order.end(); ++it)
        {
            auto & curr_ins = instruction[*it];
            auto & id = curr_ins->name;
            // DOUT("Latency compensating instruction: " << id);
            long latency_cycles=0;

            if(platform.instruction_settings.count(id) > 0)
            {
                if(platform.instruction_settings[id].count("latency") > 0)
                {
                    float latency_ns = platform.instruction_settings[id]["latency"];
                    latency_cycles = (std::ceil( static_cast<float>(std::abs(latency_ns)) / cycle_time)) *
                                            ql::utils::sign_of(latency_ns);
                }
            }
            cycle[*it] = cycle[*it] + latency_cycles;
            // DOUT( cycle[*it] << " <- " << name[*it] << latency_cycles );
        }

        COUT("Re-ordering ...");
        // re-order
        std::sort
            (
                order.begin(),
                order.end(),
                [&](ListDigraph::Node & n1, ListDigraph::Node & n2) { return cycle[n1] > cycle[n2]; }
            );

        // DOUT("Printing ASAP Schedule after latency compensation");
        // DOUT("Cycle    Instruction");
        // for ( auto it = order.rbegin(); it != order.rend(); ++it)
        // {
        //     DOUT( cycle[*it] << "        " << name[*it] );
        // }

        DOUT("Performing RC ASAP Scheduling [Done].");
    }

    // void PrintScheduleASAP()
    // {
    //     ListDigraph::NodeMap<size_t> cycle(graph);
    //     std::vector<ListDigraph::Node> order;
    //     schedule_asap_(cycle,order);

    //     COUT("\nPrinting ASAP Schedule");
    //     std::cout << "Cycle <- Instruction " << std::endl;
    //     std::vector<ListDigraph::Node>::reverse_iterator it;
    //     for ( it = order.rbegin(); it != order.rend(); ++it)
    //     {
    //         std::cout << cycle[*it] << "     <- " <<  name[*it] << std::endl;
    //     }
    // }

    // std::string GetDotScheduleASAP()
    // {
    //     stringstream dotout;
    //     ListDigraph::NodeMap<size_t> cycle(graph);
    //     std::vector<ListDigraph::Node> order;
    //     schedule_asap_(cycle,order);
    //     PrintDot1_(false,true,cycle,order,dotout);
    //     return dotout.str();
    // }

    // void PrintDotScheduleASAP()
    // {
    //     ofstream dotout;
    //     string dotfname( ql::options::get("output_dir") + "/scheduledASAP.dot");
    //     dotout.open( dotfname, ios::binary);
    //     if ( dotout.fail() )
    //     {
    //         EOUT("opening file " << dotfname << std::endl
    //                  << "Make sure the output directory ("<< ql::options::get("output_dir") << ") exists");
    //         return;
    //     }

    //     IOUT("Printing Scheduled Graph in " << dotfname);
    //     dotout << GetDotScheduleASAP();
    //     dotout.close();
    // }


    ql::ir::bundles_t schedule_asap_pre179()
    {
        DOUT("Scheduling ASAP to get bundles ...");
        ql::ir::bundles_t bundles;
        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        schedule_asap_(cycle, order);

        typedef std::vector<ql::gate*> insInOneCycle;
        std::map<size_t,insInOneCycle> insInAllCycles;

        std::vector<ListDigraph::Node>::reverse_iterator rit;
        for ( rit = order.rbegin(); rit != order.rend(); ++rit)
        {
            if( instruction[*rit]->type() != ql::gate_type_t::__wait_gate__ )
                insInAllCycles[ cycle[*rit] ].push_back( instruction[*rit] );
        }

        size_t TotalCycles = 0;
        if( ! order.empty() )
        {
            // order.rbegin==SOURCE, order.begin==SINK
            TotalCycles =  cycle[ *( order.begin() ) ];
        }

        for(size_t currCycle=1; currCycle<TotalCycles; ++currCycle)
        {
            auto it = insInAllCycles.find(currCycle);
            ql::ir::bundle_t abundle;
            abundle.start_cycle = currCycle;
            size_t bduration = 0;
            if( it != insInAllCycles.end() )
            {
                auto nInsThisCycle = insInAllCycles[currCycle].size();
                for(size_t i=0; i<nInsThisCycle; ++i )
                {
                    ql::ir::section_t asec;
                    auto & ins = insInAllCycles[currCycle][i];
                    asec.push_back(ins);
                    abundle.parallel_sections.push_back(asec);
                    size_t iduration = ins->duration;
                    bduration = std::max(bduration, iduration);
                }
                abundle.duration_in_cycles = std::ceil(static_cast<float>(bduration)/cycle_time);
                bundles.push_back(abundle);
            }
        }
        if( ! order.empty() )
        {
            // Totalcycles == cycle[t] (i.e. of SINK), and includes SOURCE with its duration
            DOUT("Depth: " << TotalCycles-bundles.front().start_cycle);
        }
        else
        {
            DOUT("Depth: " << 0);
        }

        DOUT("Scheduling ASAP to get bundles [DONE]");
        return bundles;
    }



    // the following with rc and buffer-buffer delays
    ql::ir::bundles_t schedule_asap_pre179(ql::arch::resource_manager_t & rm, const ql::quantum_platform & platform)
    {
        DOUT("RC Scheduling ASAP to get bundles ...");
        ql::ir::bundles_t bundles;
        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        schedule_asap_(cycle, order, rm, platform);

        typedef std::vector<ql::gate*> insInOneCycle;
        std::map<size_t,insInOneCycle> insInAllCycles;

        std::vector<ListDigraph::Node>::iterator it;
        for ( it = order.begin(); it != order.end(); ++it)
        {
            if ( instruction[*it]->type() != ql::gate_type_t::__wait_gate__ &&
                 instruction[*it]->type() != ql::gate_type_t::__dummy_gate__
               )
            {
                insInAllCycles[ cycle[*it] ].push_back( instruction[*it] );
            }
        }

        size_t TotalCycles = 0;
        if( ! order.empty() )
        {
            // order.rbegin==SOURCE, order.begin==SINK
            TotalCycles =  cycle[ *( order.begin() ) ];
        }

        for(size_t currCycle = 0; currCycle<=TotalCycles; ++currCycle)
        {
            auto it = insInAllCycles.find(currCycle);
            if( it != insInAllCycles.end() )
            {
                ql::ir::bundle_t abundle;
                size_t bduration = 0;
                auto nInsThisCycle = insInAllCycles[currCycle].size();
                for(size_t i=0; i<nInsThisCycle; ++i )
                {
                    ql::ir::section_t aparsec;
                    auto & ins = insInAllCycles[currCycle][i];
                    aparsec.push_back(ins);
                    abundle.parallel_sections.push_back(aparsec);
                    size_t iduration = ins->duration;
                    bduration = std::max(bduration, iduration);
                }
                abundle.start_cycle = currCycle;
                abundle.duration_in_cycles = std::ceil(static_cast<float>(bduration)/cycle_time);
                bundles.push_back(abundle);
            }
        }
        if( ! order.empty() )
        {
            // Totalcycles == cycle[t] (i.e. of SINK), and includes SOURCE with its duration
            DOUT("Depth: " << TotalCycles-bundles.front().start_cycle);
        }
        else
        {
            DOUT("Depth: " << 0);
        }

        // insert buffer - buffer delays
        DOUT("buffer-buffer delay insertion ... ");
        std::vector<std::string> operations_prev_bundle;
        size_t buffer_cycles_accum = 0;
        for(ql::ir::bundle_t & abundle : bundles)
        {
            std::vector<std::string> operations_curr_bundle;
            for( auto secIt = abundle.parallel_sections.begin(); secIt != abundle.parallel_sections.end(); ++secIt )
            {
                for(auto insIt = secIt->begin(); insIt != secIt->end(); ++insIt )
                {
                    auto & id = (*insIt)->name;
                    std::string op_type("none");
                    if(platform.instruction_settings.count(id) > 0)
                    {
                        if(platform.instruction_settings[id].count("type") > 0)
                        {
                            op_type = platform.instruction_settings[id]["type"];
                        }
                    }
                    operations_curr_bundle.push_back(op_type);
                }
            }

            size_t buffer_cycles = 0;
            for(auto & op_prev : operations_prev_bundle)
            {
                for(auto & op_curr : operations_curr_bundle)
                {
                    auto temp_buf_cycles = buffer_cycles_map[ std::pair<std::string,std::string>(op_prev, op_curr) ];
                    DOUT("Considering buffer_" << op_prev << "_" << op_curr << ": " << temp_buf_cycles);
                    buffer_cycles = std::max(temp_buf_cycles, buffer_cycles);
                }
            }
            DOUT( "Inserting buffer : " << buffer_cycles);
            buffer_cycles_accum += buffer_cycles;
            abundle.start_cycle = abundle.start_cycle + buffer_cycles_accum;
            operations_prev_bundle = operations_curr_bundle;
        }

        DOUT("RC Scheduling ASAP to get bundles [DONE]");
        return bundles;
    }

// =========== pre179 alap

    void schedule_alap_(ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order)
    {
        DOUT("Performing ALAP Scheduling");
        TopologicalSort(order);

        std::vector<ListDigraph::Node>::iterator currNode = order.begin();
        cycle[*currNode]=MAX_CYCLE;
        ++currNode;
        while( currNode != order.end() )
        {
            // DOUT("Scheduling " << name[*currNode]);
            size_t currCycle=MAX_CYCLE;
            for( ListDigraph::OutArcIt arc(graph,*currNode); arc != INVALID; ++arc )
            {
                ListDigraph::Node targetNode  = graph.target(arc);
                size_t targetCycle = cycle[targetNode];
                if(currCycle > (targetCycle-weight[arc]) )
                {
                    currCycle = targetCycle - weight[arc];
                }
            }
            cycle[*currNode]=currCycle;
            ++currNode;
        }
        // DOUT("Printing ALAP Schedule");
        // DOUT("Cycle   Cycle-simplified    Instruction");
        // for ( auto it = order.begin(); it != order.end(); ++it)
        // {
        //     DOUT( cycle[*it] << " :: " << MAX_CYCLE-cycle[*it] << "  <- " << name[*it] );
        // }
        DOUT("Performing ALAP Scheduling [Done].");
    }

    // with rc and latency compensation
    void schedule_alap_( ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order,
                       ql::arch::resource_manager_t & rm, const ql::quantum_platform & platform)
    {
        DOUT("Performing RC ALAP Scheduling");

        TopologicalSort(order);

        std::vector<ListDigraph::Node>::iterator currNode = order.begin();
        cycle[*currNode]=MAX_CYCLE;          // sink node
        ++currNode;
        while(currNode != order.end() )
        {
            DOUT("");
            auto & curr_ins = instruction[*currNode];
            auto & id = curr_ins->name;

            size_t op_start_cycle=MAX_CYCLE;
            DOUT("Scheduling " << name[*currNode]);
            for( ListDigraph::OutArcIt arc(graph,*currNode); arc != INVALID; ++arc )
            {
                ListDigraph::Node targetNode  = graph.target(arc);
                size_t targetCycle = cycle[targetNode];
                if(op_start_cycle > (targetCycle - weight[arc]))
                {
                    op_start_cycle = targetCycle - weight[arc];
                }
            }

            if(curr_ins->type() == ql::gate_type_t::__dummy_gate__ ||
               curr_ins->type() == ql::gate_type_t::__classical_gate__)
            {
                cycle[*currNode]=op_start_cycle;
            }
            else
            {
                std::string operation_name(id);
                std::string operation_type; // MW/FLUX/READOUT etc
                std::string instruction_type; // single / two qubit
                size_t operation_duration = std::ceil( static_cast<float>(curr_ins->duration) / cycle_time);

                if(platform.instruction_settings.count(id) > 0)
                {
                    if(platform.instruction_settings[id].count("cc_light_instr") > 0)
                    {
                        operation_name = platform.instruction_settings[id]["cc_light_instr"];
                    }
                    if(platform.instruction_settings[id].count("type") > 0)
                    {
                        operation_type = platform.instruction_settings[id]["type"];
                    }
                    if(platform.instruction_settings[id].count("cc_light_instr_type") > 0)
                    {
                        instruction_type = platform.instruction_settings[id]["cc_light_instr_type"];
                    }
                }

                while(op_start_cycle > 0)
                {
                    DOUT("Trying to schedule: " << name[*currNode] << "  in cycle: " << op_start_cycle);
                    DOUT("current operation_duration: " << operation_duration);
                    if( rm.available(op_start_cycle, curr_ins, operation_name, operation_type, instruction_type, operation_duration) )
                    {
                        DOUT("Resources available at cycle " << op_start_cycle << ", Scheduled.");

                        rm.reserve(op_start_cycle, curr_ins, operation_name, operation_type, instruction_type, operation_duration);
                        cycle[*currNode]=op_start_cycle;
                        break;
                    }
                    else
                    {
                        DOUT("Resources not available at cycle " << op_start_cycle << ", trying again ...");
                        --op_start_cycle;
                    }
                }
                if(op_start_cycle <= 0)
                {
                    EOUT("Error: could not find schedule");
                    throw ql::exception("[x] Error : could not find schedule !",false);
                }
            }
            ++currNode;
        }

        // DOUT("Printing ALAP Schedule before latency compensation");
        // DOUT("Cycle   Cycle-simplified    Instruction");
        // for ( auto it = order.begin(); it != order.end(); ++it)
        // {
        //     DOUT( cycle[*it] << " :: " << MAX_CYCLE-cycle[*it] << "  <- " << name[*it] );
        // }

        // latency compensation
        for ( auto it = order.begin(); it != order.end(); ++it)
        {
            auto & curr_ins = instruction[*it];
            auto & id = curr_ins->name;
            long latency_cycles=0;
            if(platform.instruction_settings.count(id) > 0)
            {
                if(platform.instruction_settings[id].count("latency") > 0)
                {
                    float latency_ns = platform.instruction_settings[id]["latency"];
                    latency_cycles = (std::ceil( static_cast<float>(std::abs(latency_ns)) / cycle_time)) *
                                            ql::utils::sign_of(latency_ns);
                }
            }
            cycle[*it] = cycle[*it] + latency_cycles;
            // DOUT( cycle[*it] << " <- " << name[*it] << latency_cycles );
        }

        // re-order
        std::sort
            (
                order.begin(),
                order.end(),
                [&](ListDigraph::Node & n1, ListDigraph::Node & n2) { return cycle[n1] > cycle[n2]; }
            );

        // DOUT("Printing ALAP Schedule after latency compensation");
        // DOUT("Cycle    Instruction");
        // for ( auto it = order.begin(); it != order.end(); ++it)
        // {
        //     DOUT( cycle[*it] << "        " << name[*it] );
        // }

        DOUT("Performing RC ALAP Scheduling [Done].");
    }

    // void PrintScheduleALAP()
    // {
    //     ListDigraph::NodeMap<size_t> cycle(graph);
    //     std::vector<ListDigraph::Node> order;
    //     schedule_alap_(cycle,order);

    //     COUT("\nPrinting ALAP Schedule");
    //     std::cout << "Cycle <- Instruction " << std::endl;
    //     std::vector<ListDigraph::Node>::reverse_iterator it;
    //     for ( it = order.rbegin(); it != order.rend(); ++it)
    //     {
    //         std::cout << MAX_CYCLE-cycle[*it] << "     <- " <<  name[*it] << std::endl;
    //     }
    // }

    // void PrintDot2_(
    //             bool WithCritical,
    //             bool WithCycles,
    //             ListDigraph::NodeMap<size_t> & cycle,
    //             std::vector<ListDigraph::Node> & order,
    //             std::ostream& dotout
    //             )
    // {
    //     ListDigraph::ArcMap<bool> isInCritical(graph);
    //     if(WithCritical)
    //     {
    //         for (ListDigraph::ArcIt a(graph); a != INVALID; ++a)
    //         {
    //             isInCritical[a] = false;
    //             for ( Path<ListDigraph>::ArcIt ap(p); ap != INVALID; ++ap )
    //             {
    //                 if(a==ap)
    //                 {
    //                     isInCritical[a] = true;
    //                     break;
    //                 }
    //             }
    //         }
    //     }

    //     string NodeStyle(" fontcolor=black, style=filled, fontsize=16");
    //     string EdgeStyle1(" color=black");
    //     string EdgeStyle2(" color=red");
    //     string EdgeStyle = EdgeStyle1;

    //     dotout << "digraph {\ngraph [ rankdir=TD; ]; // or rankdir=LR"
    //         << "\nedge [fontsize=16, arrowhead=vee, arrowsize=0.5];"
    //         << endl;

    //     // first print the nodes
    //     for (ListDigraph::NodeIt n(graph); n != INVALID; ++n)
    //     {
    //         int nid = graph.id(n);
    //         string nodeName = name[n];
    //         dotout  << "\"" << nid << "\""
    //                 << " [label=\" " << nodeName <<" \""
    //                 << NodeStyle
    //                 << "];" << endl;
    //     }

    //     if( WithCycles)
    //     {
    //         // Print cycle numbers as timeline, as shown below
    //         size_t cn=0,TotalCycles = MAX_CYCLE - cycle[ *( order.rbegin() ) ];
    //         dotout << "{\nnode [shape=plaintext, fontsize=16, fontcolor=blue]; \n";

    //         for(cn=0;cn<=TotalCycles;++cn)
    //         {
    //             if(cn>0)
    //                 dotout << " -> ";
    //             dotout << "Cycle" << cn;
    //         }
    //         dotout << ";\n}\n";

    //         // Now print ranks, as shown below
    //         std::vector<ListDigraph::Node>::reverse_iterator rit;
    //         for ( rit = order.rbegin(); rit != order.rend(); ++rit)
    //         {
    //             int nid = graph.id(*rit);
    //             dotout << "{ rank=same; Cycle" << TotalCycles - (MAX_CYCLE - cycle[*rit]) <<"; " <<nid<<"; }\n";
    //         }
    //     }

    //     // now print the edges
    //     for (ListDigraph::ArcIt arc(graph); arc != INVALID; ++arc)
    //     {
    //         ListDigraph::Node srcNode = graph.source(arc);
    //         ListDigraph::Node dstNode = graph.target(arc);
    //         int srcID = graph.id( srcNode );
    //         int dstID = graph.id( dstNode );

    //         if(WithCritical)
    //             EdgeStyle = ( isInCritical[arc]==true ) ? EdgeStyle2 : EdgeStyle1;

    //         dotout << dec
    //             << "\"" << srcID << "\""
    //             << "->"
    //             << "\"" << dstID << "\""
    //             << "[ label=\""
    //             << "q" << cause[arc]
    //             << " , " << weight[arc]
    //             << " , " << DepTypesNames[ depType[arc] ]
    //             <<"\""
    //             << " " << EdgeStyle << " "
    //             << "]"
    //             << endl;
    //     }

    //     dotout << "}" << endl;
    // }

    // void PrintDotScheduleALAP()
    // {
    //     ofstream dotout;
    //     string dotfname(ql::options::get("output_dir") + "/scheduledALAP.dot");
    //     dotout.open( dotfname, ios::binary);
    //     if ( dotout.fail() )
    //     {
    //         EOUT("Error opening file " << dotfname << std::endl
    //                  << "Make sure the output directory ("<< ql::options::get("output_dir") << ") exists");
    //         return;
    //     }

    //     IOUT("Printing Scheduled Graph in " << dotfname);
    //     ListDigraph::NodeMap<size_t> cycle(graph);
    //     std::vector<ListDigraph::Node> order;
    //     schedule_alap_(cycle,order);
    //     PrintDot2_(false,true,cycle,order,dotout);

    //     dotout.close();
    // }

    // std::string GetDotScheduleALAP()
    // {
    //     stringstream dotout;
    //     ListDigraph::NodeMap<size_t> cycle(graph);
    //     std::vector<ListDigraph::Node> order;
    //     schedule_alap_(cycle,order);
    //     PrintDot2_(false,true,cycle,order,dotout);
    //     return dotout.str();
    // }


    // the following without rc and buffer-buffer delays
    ql::ir::bundles_t schedule_alap_pre179()
    {
        DOUT("Scheduling ALAP to get bundles ...");
        ql::ir::bundles_t bundles;
        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        schedule_alap_(cycle,order);

        typedef std::vector<ql::gate*> insInOneCycle;
        std::map<size_t,insInOneCycle> insInAllCycles;

        std::vector<ListDigraph::Node>::iterator it;
        for ( it = order.begin(); it != order.end(); ++it)
        {
            if( instruction[*it]->type() != ql::gate_type_t::__wait_gate__ )
                insInAllCycles[ MAX_CYCLE - cycle[*it] ].push_back( instruction[*it] );
        }

        size_t TotalCycles = 0;
        if( ! order.empty() )
        {
            // order.rbegin==SOURCE, order.begin==SINK
            TotalCycles =  MAX_CYCLE - cycle[ *( order.rbegin() ) ];
        }

        for(size_t currCycle = TotalCycles-1; currCycle>0; --currCycle)
        {
            auto it = insInAllCycles.find(currCycle);
            ql::ir::bundle_t abundle;
            abundle.start_cycle = TotalCycles - currCycle;
            size_t bduration = 0;
            if( it != insInAllCycles.end() )
            {
                auto nInsThisCycle = insInAllCycles[currCycle].size();
                for(size_t i=0; i<nInsThisCycle; ++i )
                {
                    ql::ir::section_t asec;
                    auto & ins = insInAllCycles[currCycle][i];
                    asec.push_back(ins);
                    abundle.parallel_sections.push_back(asec);
                    size_t iduration = ins->duration;
                    bduration = std::max(bduration, iduration);
                }
                abundle.duration_in_cycles = std::ceil(static_cast<float>(bduration)/cycle_time);
                bundles.push_back(abundle);
            }
        }
        if( ! order.empty() )
        {
            // Totalcycles == MAX_CYCLE-(MAX_CYCLE-cycle[s]) (i.e. of SOURCE) and includes SOURCE with duration 1
            DOUT("Depth: " << TotalCycles-bundles.front().start_cycle);
        }
        else
        {
            DOUT("Depth: " << 0);
        }

        DOUT("Scheduling ALAP to get bundles [DONE]");
        return bundles;
    }

    // the following with rc and buffer-buffer delays
    ql::ir::bundles_t schedule_alap_pre179(ql::arch::resource_manager_t & rm, 
        const ql::quantum_platform & platform)
    {
        DOUT("RC Scheduling ALAP to get bundles ...");
        ql::ir::bundles_t bundles;
        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        schedule_alap_(cycle, order, rm, platform);

        typedef std::vector<ql::gate*> insInOneCycle;
        std::map<size_t,insInOneCycle> insInAllCycles;

        std::vector<ListDigraph::Node>::iterator it;
        for ( it = order.begin(); it != order.end(); ++it)
        {
            if ( instruction[*it]->type() != ql::gate_type_t::__wait_gate__ &&
                 instruction[*it]->type() != ql::gate_type_t::__dummy_gate__
               )
            {
                insInAllCycles[ MAX_CYCLE - cycle[*it] ].push_back( instruction[*it] );
            }
        }

        size_t TotalCycles = 0;
        if( ! order.empty() )
        {
            TotalCycles =  MAX_CYCLE - cycle[ *( order.rbegin() ) ];
        }

        for(size_t currCycle = TotalCycles-1; currCycle>0; --currCycle)
        {
            auto it = insInAllCycles.find(currCycle);
            ql::ir::bundle_t abundle;
            abundle.start_cycle = TotalCycles - currCycle;
            size_t bduration = 0;
            if( it != insInAllCycles.end() )
            {
                auto nInsThisCycle = insInAllCycles[currCycle].size();
                for(size_t i=0; i<nInsThisCycle; ++i )
                {
                    ql::ir::section_t asec;
                    auto & ins = insInAllCycles[currCycle][i];
                    asec.push_back(ins);
                    abundle.parallel_sections.push_back(asec);
                    size_t iduration = ins->duration;
                    bduration = std::max(bduration, iduration);
                }
                abundle.duration_in_cycles = std::ceil(static_cast<float>(bduration)/cycle_time);
                bundles.push_back(abundle);
            }
        }
        if( ! order.empty() )
        {
            // Totalcycles == MAX_CYCLE-(MAX_CYCLE-cycle[s]) (i.e. of SOURCE) and includes SOURCE with duration 1
            DOUT("Depth: " << TotalCycles-bundles.front().start_cycle);
        }
        else
        {
            DOUT("Depth: " << 0);
        }

        // insert buffer - buffer delays
        DOUT("buffer-buffer delay insertion ... ");
        std::vector<std::string> operations_prev_bundle;
        size_t buffer_cycles_accum = 0;
        for(ql::ir::bundle_t & abundle : bundles)
        {
            std::vector<std::string> operations_curr_bundle;
            for( auto secIt = abundle.parallel_sections.begin(); secIt != abundle.parallel_sections.end(); ++secIt )
            {
                for(auto insIt = secIt->begin(); insIt != secIt->end(); ++insIt )
                {
                    auto & id = (*insIt)->name;
                    std::string op_type("none");
                    if(platform.instruction_settings.count(id) > 0)
                    {
                        if(platform.instruction_settings[id].count("type") > 0)
                        {
                            op_type = platform.instruction_settings[id]["type"];
                        }
                    }
                    operations_curr_bundle.push_back(op_type);
                }
            }

            size_t buffer_cycles = 0;
            for(auto & op_prev : operations_prev_bundle)
            {
                for(auto & op_curr : operations_curr_bundle)
                {
                    auto temp_buf_cycles = buffer_cycles_map[ std::pair<std::string,std::string>(op_prev, op_curr) ];
                    DOUT("Considering buffer_" << op_prev << "_" << op_curr << ": " << temp_buf_cycles);
                    buffer_cycles = std::max(temp_buf_cycles, buffer_cycles);
                }
            }
            DOUT( "Inserting buffer : " << buffer_cycles);
            buffer_cycles_accum += buffer_cycles;
            abundle.start_cycle = abundle.start_cycle + buffer_cycles_accum;
            operations_prev_bundle = operations_curr_bundle;
        }

        DOUT("RC Scheduling ALAP to get bundles [DONE]");
        return bundles;
    }


// =========== pre179 uniform
    void compute_alap_cycle(ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order, size_t max_cycle)
    {
        // DOUT("Computing alap_cycle");
        std::vector<ListDigraph::Node>::iterator currNode = order.begin();
        cycle[*currNode]=max_cycle;
        ++currNode;
        while( currNode != order.end() )
        {
            // DOUT("Scheduling " << name[*currNode]);
            size_t currCycle=max_cycle;
            for( ListDigraph::OutArcIt arc(graph,*currNode); arc != INVALID; ++arc )
            {
                ListDigraph::Node targetNode  = graph.target(arc);
                size_t targetCycle = cycle[targetNode];
                if(currCycle > (targetCycle-weight[arc]) )
                {
                    currCycle = targetCycle - weight[arc];
                }
            }
            cycle[*currNode]=currCycle;
            ++currNode;
        }
    }

    void compute_asap_cycle(ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order)
    {
        // DOUT("Computing asap_cycle");
        std::vector<ListDigraph::Node>::reverse_iterator currNode = order.rbegin();
        cycle[*currNode]=0; // src dummy in cycle 0
        ++currNode;
        while(currNode != order.rend() )
        {
            size_t currCycle=0;
            // DOUT("Scheduling " << name[*currNode]);
            for( ListDigraph::InArcIt arc(graph,*currNode); arc != INVALID; ++arc )
            {
                ListDigraph::Node srcNode  = graph.source(arc);
                size_t srcCycle = cycle[srcNode];
                if(currCycle < (srcCycle + weight[arc]))
                {
                    currCycle = srcCycle + weight[arc];
                }
            }
            cycle[*currNode]=currCycle;
            ++currNode;
        }
    }


    void schedule_alap_uniform_(ListDigraph::NodeMap<size_t> & cycle, std::vector<ListDigraph::Node> & order)
    {
        // algorithm based on "Balanced Scheduling and Operation Chaining in High-Level Synthesis for FPGA Designs"
        // by David C. Zaretsky, Gaurav Mittal, Robert P. Dick, and Prith Banerjee
        // Figure 3. Balanced scheduling algorithm
        // Modifications:
        // - dependency analysis in article figure 2 is O(n^2) because of set union
        //   this has been left out, using our own linear dependency analysis creating a digraph
        //   and using the alap values as measure instead of the dep set size computed in article's D[n]
        // - balanced scheduling algorithm dominates with its O(n^2) when it cannot find a node to forward
        //   no test has been devised yet to break the loop (figure 3, line 14-35)
        // - targeted bundle size is adjusted each cycle and is number_of_gates_to_go/number_of_non_empty_bundles_to_go
        //   this is more greedy, preventing oscillation around a target size based on all bundles,
        //   because local variations caused by local dep chains create small bundles and thus leave more gates still to go
        //
        // Oddly enough, it starts off with an ASAP schedule.
        // After this, it moves nodes up at most to their ALAP cycle to fill small bundles to the targeted uniform length.
        // It does this in a backward scan (as ALAP scheduling would do), so bundles at the highest cycles are filled first.
        // Hence, the result resembles an ALAP schedule with excess bundle lengths solved by moving nodes down ("dough rolling").

        DOUT("Performing ALAP UNIFORM Scheduling");
        // order becomes a reversed topological order of the nodes
        // don't know why it is done, since the nodes already are in topological order
        // that they are is a consequence of dep graph computation which is based on this original order
        TopologicalSort(order);

        // compute cycle itself as asap as first approximation of result
        // when schedule is already uniform, nothing changes
        // this actually is schedule_asap_(cycle, order) but without topological sort
        compute_asap_cycle(cycle, order);
        size_t   cycle_count = cycle[*( order.begin() )];// order is reversed asap, so starts at cycle_count

        // compute alap_cycle
        // when making asap bundles uniform in size in backward scan
        // fill them by moving instructions from earlier bundles (lower cycle values)
        // prefer to move those with highest alap (because that maximizes freedom)
        ListDigraph::NodeMap<size_t> alap_cycle(graph);
        compute_alap_cycle(alap_cycle, order, cycle_count);

        // DOUT("Creating nodes_per_cycle");
        // create nodes_per_cycle[cycle] = for each cycle the list of nodes at cycle cycle
        // this is the basic map to be operated upon by the uniforming scheduler below;
        // gate_count is computed to compute the target bundle size later
        std::map<size_t,std::list<ListDigraph::Node>> nodes_per_cycle;
        std::vector<ListDigraph::Node>::iterator it;
        for ( it = order.begin(); it != order.end(); ++it)
        {
            nodes_per_cycle[ cycle[*it] ].push_back( *it );
        }

        // DOUT("Displaying circuit and bundle statistics");
        // to compute how well the algorithm is doing, two measures are computed:
        // - the largest number of gates in a cycle in the circuit,
        // - and the average number of gates in non-empty cycles
        // this is done before and after uniform scheduling, and printed
        size_t max_gates_per_cycle = 0;
        size_t non_empty_bundle_count = 0;
        size_t gate_count = 0;
        for (size_t curr_cycle = 0; curr_cycle != cycle_count; curr_cycle++)
        {
            max_gates_per_cycle = std::max(max_gates_per_cycle, nodes_per_cycle[curr_cycle].size());
            if (int(nodes_per_cycle[curr_cycle].size()) != 0) non_empty_bundle_count++;
            gate_count += nodes_per_cycle[curr_cycle].size();
        }
        double avg_gates_per_cycle = double(gate_count)/cycle_count;
        double avg_gates_per_non_empty_cycle = double(gate_count)/non_empty_bundle_count;
        IOUT("... before uniform scheduling:"
            << " cycle_count=" << cycle_count
            << "; gate_count=" << gate_count
            << "; non_empty_bundle_count=" << non_empty_bundle_count
            );
        IOUT("... and max_gates_per_cycle=" << max_gates_per_cycle
            << "; avg_gates_per_cycle=" << avg_gates_per_cycle
            << "; ..._per_non_empty_cycle=" << avg_gates_per_non_empty_cycle
            );

        // backward make bundles max avg_gates_per_cycle long
        // DOUT("Backward scan uniform scheduling ILP");
        for (size_t curr_cycle = cycle_count-1; curr_cycle != 0; curr_cycle--)    // QUESTION: gate at cycle 0?
        {
            // Backward with pred_cycle from curr_cycle-1, look for node(s) to extend current too small bundle.
            // This assumes that current bundle is never too long, excess having been moved away earlier.
            // When such a node cannot be found, this loop scans the whole circuit for each original node to extend
            // and this creates a O(n^2) time complexity.
            //
            // A test to break this prematurely based on the current data structure, wasn't devised yet.
            // A sulution is to use the dep graph instead to find a node to extend the current node with,
            // i.e. maintain a so-called "heap" of nodes free to schedule, as in conventional scheduling algorithms,
            // which is not hard at all but which is not according to the published algorithm.
            // When the complexity becomes a problem, it is proposed to rewrite the algorithm accordingly.

            long pred_cycle = curr_cycle - 1;    // signed because can become negative

            // target size of each bundle is number of gates to go divided by number of non-empty cycles to go
            // it averages over non-empty bundles instead of all bundles because the latter would be very strict
            // it is readjusted to cater for dips in bundle size caused by local dependence chains
            if (non_empty_bundle_count == 0) break;
            avg_gates_per_cycle = double(gate_count)/curr_cycle;
            avg_gates_per_non_empty_cycle = double(gate_count)/non_empty_bundle_count;
            DOUT("Cycle=" << curr_cycle << " number of gates=" << nodes_per_cycle[curr_cycle].size()
                << "; avg_gates_per_cycle=" << avg_gates_per_cycle
                << "; ..._per_non_empty_cycle=" << avg_gates_per_non_empty_cycle);

            while ( double(nodes_per_cycle[curr_cycle].size()) < avg_gates_per_non_empty_cycle && pred_cycle >= 0 )
            {
                size_t          max_alap_cycle = 0;
                ListDigraph::Node best_n;
                bool          best_n_found = false;

                // scan bundle at pred_cycle to find suitable candidate to move forward to curr_cycle
                for ( auto n : nodes_per_cycle[pred_cycle] )
                {
                    bool          forward_n = true;
                    size_t          n_completion_cycle;

                    // candidate's result, when moved, must be ready before end-of-circuit and before used
                    n_completion_cycle = curr_cycle + std::ceil(static_cast<float>(instruction[n]->duration)/cycle_time);
                    if (n_completion_cycle > cycle_count)
                    {
                        forward_n = false;
                    }
                    for ( ListDigraph::OutArcIt arc(graph,n); arc != INVALID; ++arc )
                    {
                        ListDigraph::Node targetNode  = graph.target(arc);
                        size_t targetCycle = cycle[targetNode];
                        if(n_completion_cycle > targetCycle)
                        {
                            forward_n = false;
                        }
                    }

                    // when multiple nodes in bundle qualify, take the one with highest alap cycle
                    if (forward_n && alap_cycle[n] > max_alap_cycle)
                    {
                        max_alap_cycle = alap_cycle[n];
                        best_n_found = true;
                        best_n = n;
                    }
                }

                // when candidate was found in this bundle, move it, and search for more in this bundle, if needed
                // otherwise, continue scanning backward
                if (best_n_found)
                {
                    nodes_per_cycle[pred_cycle].remove(best_n);
                    if (nodes_per_cycle[pred_cycle].size() == 0)
                    {
                        // bundle was non-empty, now it is empty
                        non_empty_bundle_count--;
                    }
                    if (nodes_per_cycle[curr_cycle].size() == 0)
                    {
                        // bundle was empty, now it will be non_empty
                        non_empty_bundle_count++;
                    }
                    cycle[best_n] = curr_cycle;
                    nodes_per_cycle[curr_cycle].push_back(best_n);
                    if (non_empty_bundle_count == 0) break;
                    avg_gates_per_cycle = double(gate_count)/curr_cycle;
                    avg_gates_per_non_empty_cycle = double(gate_count)/non_empty_bundle_count;
                    DOUT("... moved " << name[best_n] << " with alap=" << alap_cycle[best_n]
                        << " from cycle=" << pred_cycle << " to cycle=" << curr_cycle
                        << "; new avg_gates_per_cycle=" << avg_gates_per_cycle
                        << "; ..._per_non_empty_cycle=" << avg_gates_per_non_empty_cycle
                        );
                }
                else
                {
                    pred_cycle --;
                }
            }   // end for finding a bundle to forward a node from to the current cycle

            // curr_cycle ready, mask it from the counts and recompute counts for remaining cycles
            gate_count -= nodes_per_cycle[curr_cycle].size();
            if (nodes_per_cycle[curr_cycle].size() != 0)
            {
                // bundle is non-empty
                non_empty_bundle_count--;
            }
        }   // end curr_cycle loop; curr_cycle is bundle which must be enlarged when too small

        // Recompute and print statistics reporting on uniform scheduling performance
        max_gates_per_cycle = 0;
        non_empty_bundle_count = 0;
        gate_count = 0;
        // cycle_count was not changed
        for (size_t curr_cycle = 0; curr_cycle != cycle_count; curr_cycle++)
        {
            max_gates_per_cycle = std::max(max_gates_per_cycle, nodes_per_cycle[curr_cycle].size());
            if (int(nodes_per_cycle[curr_cycle].size()) != 0) non_empty_bundle_count++;
            gate_count += nodes_per_cycle[curr_cycle].size();
        }
        avg_gates_per_cycle = double(gate_count)/cycle_count;
        avg_gates_per_non_empty_cycle = double(gate_count)/non_empty_bundle_count;
        IOUT("... after uniform scheduling:"
            << " cycle_count=" << cycle_count
            << "; gate_count=" << gate_count
            << "; non_empty_bundle_count=" << non_empty_bundle_count
            );
        IOUT("... and max_gates_per_cycle=" << max_gates_per_cycle
            << "; avg_gates_per_cycle=" << avg_gates_per_cycle
            << "; ..._per_non_empty_cycle=" << avg_gates_per_non_empty_cycle
            );

        DOUT("Performing ALAP UNIFORM Scheduling [DONE]");
    }

    ql::ir::bundles_t schedule_alap_uniform_pre179()
    {
        DOUT("Scheduling ALAP UNIFORM to get bundles ...");
        ql::ir::bundles_t bundles;
        ListDigraph::NodeMap<size_t> cycle(graph);
        std::vector<ListDigraph::Node> order;
        schedule_alap_uniform_(cycle, order);

        typedef std::vector<ql::gate*> insInOneCycle;
        std::map<size_t,insInOneCycle> insInAllCycles;

        std::vector<ListDigraph::Node>::reverse_iterator rit;
        for ( rit = order.rbegin(); rit != order.rend(); ++rit)
        {
            if( instruction[*rit]->type() != ql::gate_type_t::__wait_gate__ )
                insInAllCycles[ cycle[*rit] ].push_back( instruction[*rit] );
        }

        size_t TotalCycles = 0;
        if( ! order.empty() )
        {
            TotalCycles =  cycle[ *( order.begin() ) ];
        }

        for(size_t currCycle=1; currCycle<TotalCycles; ++currCycle)
        {
            auto it = insInAllCycles.find(currCycle);
            ql::ir::bundle_t abundle;
            abundle.start_cycle = currCycle;
            size_t bduration = 0;
            if( it != insInAllCycles.end() )
            {
                auto nInsThisCycle = insInAllCycles[currCycle].size();
                for(size_t i=0; i<nInsThisCycle; ++i )
                {
                    ql::ir::section_t asec;
                    auto & ins = insInAllCycles[currCycle][i];
                    asec.push_back(ins);
                    abundle.parallel_sections.push_back(asec);
                    size_t iduration = ins->duration;
                    bduration = std::max(bduration, iduration);
                }
                abundle.duration_in_cycles = std::ceil(static_cast<float>(bduration)/cycle_time);
                bundles.push_back(abundle);
            }
        }
        if( ! order.empty() )
        {
            DOUT("Depth: " << TotalCycles-bundles.front().start_cycle);
        }
        else
        {
            DOUT("Depth: " << 0);
        }

        DOUT("Scheduling ALAP UNIFORM to get bundles [DONE]");
        return bundles;
    }

// =========== post179 plain schedulers, just ASAP and ALAP, no resources, etc.

/*
    Summary

    The post179 schedulers are linear list schedulers, i.e.
    - they scan linearly through the code, forward or backward
    - and while doing, they maintain a list of gates, of gates that are available for being scheduled
      because they are not blocked by dependences on non-scheduled gates.
    Therefore, the post179 schedulers are able to select the best one from multiple available gates.
    Not all gates that are available (not blocked by dependences on non-scheduled gates) can actually be scheduled.
    It must be made sure in addition that:
    - those scheduled gates that it depends on, actually have completed their execution
    - the resources are available for it
    Furthermore, making a selection from the nodes that remain determines the optimality of the scheduling result.
    The schedulers below are critical path schedulers, i.e. they prefer to schedule the most critical node first.
    The criticality of a node is measured by estimating
    the effect of delaying scheduling it on the depth of the resulting circuit.

    The schedulers don't actually scan the circuit themselves but rely on a dependence graph representation of the circuit.
    At the start, depending on the scheduling direction, only the top (or bottom) node is available.
    Then one by one, according to an optimality criterion, a node is selected from the list of available ones
    and added to the schedule. Having scheduled the node, it is taken out of the available list;
    also having scheduled a node, some new nodes may become available because they don't depend on non-scheduled nodes anymore;
    those nodes are found and put in the available list of nodes.
    This continues, filling cycle by cycle from low to high (or from high to low when scheduling backward),
    until the available list gets empty (which happens after scheduling the last node, the bottom (or top when backward).
*/


public:
// use MAX_CYCLE for absolute upperbound on cycle value
// use ALAP_SINK_CYCLE for initial cycle given to SINK in ALAP;
// the latter allows for some growing room when doing latency compensation/buffer-delay insertion
#define ALAP_SINK_CYCLE    (MAX_CYCLE/2)

    // cycle assignment without RC depending on direction: forward:ASAP, backward:ALAP;
    // without RC, this is all there is to schedule, apart from forming the bundles in bundler()
    // set_cycle iterates over the circuit's gates and set_cycle_gate over the dependences of each gate
    // please note that set_cycle_gate expects a caller like set_cycle which iterates gp forward through the circuit
    void set_cycle_gate(ql::gate* gp, ql::scheduling_direction_t dir)
    {
        ListDigraph::Node   currNode = node[gp];
        size_t  currCycle;
        if (ql::forward_scheduling == dir)
        {
            currCycle = 0;
            for( ListDigraph::InArcIt arc(graph,currNode); arc != INVALID; ++arc )
            {
                currCycle = std::max(currCycle, instruction[graph.source(arc)]->cycle + weight[arc]);
            }
        }
        else
        {
            currCycle = MAX_CYCLE;
            for( ListDigraph::OutArcIt arc(graph,currNode); arc != INVALID; ++arc )
            {
                currCycle = std::min(currCycle, instruction[graph.target(arc)]->cycle - weight[arc]);
            }
        }
        gp->cycle = currCycle;
    }

    void set_cycle(ql::scheduling_direction_t dir)
    {
        if (ql::forward_scheduling == dir)
        {
            instruction[s]->cycle = 0;
            DOUT("... set_cycle of " << instruction[s]->qasm() << " cycles " << instruction[s]->cycle);
            // *circp is by definition in a topological order of the dependence graph
            for ( ql::circuit::iterator gpit = circp->begin(); gpit != circp->end(); gpit++)
            {
                set_cycle_gate(*gpit, dir);
                DOUT("... set_cycle of " << (*gpit)->qasm() << " cycles " << (*gpit)->cycle);
            }
            set_cycle_gate(instruction[t], dir);
            DOUT("... set_cycle of " << instruction[t]->qasm() << " cycles " << instruction[t]->cycle);
        }
        else
        {
            instruction[t]->cycle = ALAP_SINK_CYCLE;
            // *circp is by definition in a topological order of the dependence graph
            for ( ql::circuit::reverse_iterator gpit = circp->rbegin(); gpit != circp->rend(); gpit++)
            {
                set_cycle_gate(*gpit, dir);
            }
            set_cycle_gate(instruction[s], dir);

            // readjust cycle values of gates so that SOURCE is at 0
            size_t  SOURCECycle = instruction[s]->cycle;
            DOUT("... readjusting cycle values by -" << SOURCECycle);

            instruction[t]->cycle -= SOURCECycle;
            DOUT("... set_cycle of " << instruction[t]->qasm() << " cycles " << instruction[t]->cycle);
            for ( auto & gp : *circp)
            {
                gp->cycle -= SOURCECycle;
                DOUT("... set_cycle of " << gp->qasm() << " cycles " << gp->cycle);
            }
            instruction[s]->cycle -= SOURCECycle;   // i.e. becomes 0
            DOUT("... set_cycle of " << instruction[s]->qasm() << " cycles " << instruction[s]->cycle);
        }
    }

    static bool cycle_lessthan(ql::gate* gp1, ql::gate* gp2)
    {
        return gp1->cycle < gp2->cycle;
    }

    // sort circuit by the gates' cycle attribute in non-decreasing order
    void sort_by_cycle()
    {
        // DOUT("... before sorting on cycle value");
        // for ( ql::circuit::iterator gpit = circp->begin(); gpit != circp->end(); gpit++)
        // {
        //     ql::gate*           gp = *gpit;
        //     DOUT("...... (@" << gp->cycle << ") " << gp->qasm());
        // }

        // std::sort doesn't preserve the original order of elements that have equal values but std::stable_sort does
        std::stable_sort(circp->begin(), circp->end(), cycle_lessthan);

        // DOUT("... after sorting on cycle value");
        // for ( ql::circuit::iterator gpit = circp->begin(); gpit != circp->end(); gpit++)
        // {
        //     ql::gate*           gp = *gpit;
        //     DOUT("...... (@" << gp->cycle << ") " << gp->qasm());
        // }
    }

    // return bundles for the given circuit;
    // assumes gatep->cycle attribute reflects the cycle assignment;
    // assumes circuit being a vector of gate pointers is ordered by this cycle value;
    // create bundles in a single scan over the circuit, using currBundle and currCycle as state
    ql::ir::bundles_t bundler(ql::circuit& circ)
    {
        ql::ir::bundles_t bundles;          // result bundles
    
        ql::ir::bundle_t    currBundle;     // current bundle at currCycle that is being filled
        size_t              currCycle = 0;  // cycle at which bundle is to be scheduled

        currBundle.start_cycle = currCycle; // starts off as empty bundle starting at currCycle
        currBundle.duration_in_cycles = 0;

        DOUT("bundler ...");

        for (auto & gp: circ)
        {
            // DOUT(". adding gate(@" << gp->cycle << ")  " << gp->qasm());
            if ( gp->type() == ql::gate_type_t::__wait_gate__ ||
                 gp->type() == ql::gate_type_t::__dummy_gate__
               )
            {
                DOUT("... ignoring: " << gp->qasm());
                continue;
            }
            size_t newCycle = gp->cycle;        // taking cycle values from circuit, so excludes SOURCE and SINK!
            if (newCycle < currCycle)
            {
                EOUT("Error: circuit not ordered by cycle value");
                throw ql::exception("[x] Error: circuit not ordered by cycle value",false);
            }
            if (newCycle > currCycle)
            {
                if (!currBundle.parallel_sections.empty())
                {
                    // finish currBundle at currCycle
                    // DOUT(".. bundle duration in cycles: " << currBundle.duration_in_cycles);
                    bundles.push_back(currBundle);
                    // DOUT(".. ready with bundle");
                    currBundle.parallel_sections.clear();
                }

                // new empty currBundle at newCycle
                currCycle = newCycle;
                // DOUT(".. bundling at cycle: " << currCycle);
                currBundle.start_cycle = currCycle;
                currBundle.duration_in_cycles = 0;
            }

            // add gp to currBundle
            ql::ir::section_t asec;
            asec.push_back(gp);
            currBundle.parallel_sections.push_back(asec);
            // DOUT("... gate: " << gp->qasm() << " in private parallel section");
            currBundle.duration_in_cycles = std::max(currBundle.duration_in_cycles, (gp->duration+cycle_time-1)/cycle_time); 
        }
        if (!currBundle.parallel_sections.empty())
        {
            // finish currBundle (which is last bundle) at currCycle
            // DOUT("... bundle duration in cycles: " << currBundle.duration_in_cycles);
            bundles.push_back(currBundle);
            // DOUT("... ready with bundle");
        }

        // currCycle == cycle of last gate of circuit scheduled
        // duration_in_cycles later the system starts idling
        // depth is the difference between the cycle in which it starts idling and the cycle it started execution
        DOUT("Depth: " << currCycle + currBundle.duration_in_cycles - bundles.front().start_cycle);
        DOUT("bundler [DONE]");
        return bundles;
    }

    // ASAP scheduler without RC, updating circuit and returning bundles
    ql::ir::bundles_t schedule_asap_post179()
    {
        DOUT("Scheduling ASAP post179 ...");
        set_cycle(ql::forward_scheduling);
        
        sort_by_cycle();

        DOUT("Scheduling ASAP [DONE]");
        return bundler(*circp);
    }

    // ALAP scheduler without RC, updating circuit and returning bundles
    ql::ir::bundles_t schedule_alap_post179()
    {
        DOUT("Scheduling ALAP post179 ...");
        set_cycle(ql::backward_scheduling);

        sort_by_cycle();

        DOUT("Scheduling ALAP [DONE]");
        return bundler(*circp);
    }


// =========== post179 schedulers with RC, latency compensation and buffer-buffer delay insertion
    // Most code from here on deals with scheduling with Resource Constraints.
    // Then the cycles as assigned from the depgraph shift, because of resource conflicts
    // and then at each point all available nodes should be considered for scheduling
    // to avoid largely suboptimal results (issue 179), i.e. apply list scheduling.

    // latency compensation
    void latency_compensation(ql::circuit* circp, const ql::quantum_platform& platform)
    {
        DOUT("Latency compensation ...");
        bool    compensated_one = false;
        for ( auto & gp : *circp)
        {
            auto & id = gp->name;
            // DOUT("Latency compensating instruction: " << id);
            long latency_cycles=0;

            if(platform.instruction_settings.count(id) > 0)
            {
                if(platform.instruction_settings[id].count("latency") > 0)
                {
                    float latency_ns = platform.instruction_settings[id]["latency"];
                    latency_cycles = (std::ceil( static_cast<float>(std::abs(latency_ns)) / cycle_time)) *
                                            ql::utils::sign_of(latency_ns);
                    compensated_one = true;

                    gp->cycle = gp->cycle + latency_cycles;
                    DOUT( "... compensated to @" << gp->cycle << " <- " << id << " with " << latency_cycles );
                }
            }
        }

        if (compensated_one)
        {
            DOUT("... sorting on cycle value after latency compensation");
            sort_by_cycle();

            DOUT("... printing schedule after latency compensation");
            for ( auto & gp : *circp)
            {
                DOUT("...... @(" << gp->cycle << "): " << gp->qasm());
            }
        }
        else
        {
            DOUT("... no gate latency compensated");
        }
        DOUT("Latency compensation [DONE]");
    }

    // insert buffer - buffer delays
    void insert_buffer_delays(ql::ir::bundles_t& bundles, const ql::quantum_platform& platform)
    {
        DOUT("Buffer-buffer delay insertion ... ");
        std::vector<std::string> operations_prev_bundle;
        size_t buffer_cycles_accum = 0;
        for(ql::ir::bundle_t & abundle : bundles)
        {
            std::vector<std::string> operations_curr_bundle;
            for( auto secIt = abundle.parallel_sections.begin(); secIt != abundle.parallel_sections.end(); ++secIt )
            {
                for(auto insIt = secIt->begin(); insIt != secIt->end(); ++insIt )
                {
                    auto & id = (*insIt)->name;
                    std::string op_type("none");
                    if(platform.instruction_settings.count(id) > 0)
                    {
                        if(platform.instruction_settings[id].count("type") > 0)
                        {
                            op_type = platform.instruction_settings[id]["type"];
                        }
                    }
                    operations_curr_bundle.push_back(op_type);
                }
            }

            size_t buffer_cycles = 0;
            for(auto & op_prev : operations_prev_bundle)
            {
                for(auto & op_curr : operations_curr_bundle)
                {
                    auto temp_buf_cycles = buffer_cycles_map[ std::pair<std::string,std::string>(op_prev, op_curr) ];
                    DOUT("... considering buffer_" << op_prev << "_" << op_curr << ": " << temp_buf_cycles);
                    buffer_cycles = std::max(temp_buf_cycles, buffer_cycles);
                }
            }
            DOUT( "... inserting buffer : " << buffer_cycles);
            buffer_cycles_accum += buffer_cycles;
            abundle.start_cycle = abundle.start_cycle + buffer_cycles_accum;
            operations_prev_bundle = operations_curr_bundle;
        }
        DOUT("Buffer-buffer delay insertion [DONE] ");
    }

    // In critical-path scheduling, usually more-critical instructions are preferred;
    // an instruction is more-critical when its ASAP and ALAP values differ less.
    // When scheduling with resource constraints, the ideal ASAP/ALAP cycle values cannot
    // be attained because of resource conflicts being in the way, they will 'slip',
    // so actual cycle values cannot be compared anymore to ideal ASAP/ALAP values to compute criticality;
    // but when forward (backward) scheduling, a lower ALAP (higher ASAP) indicates more criticality
    // (i.e. in ASAP scheduling use the ALAP values to know the criticality, and vice-versa);
    // those ALAP/ASAP are then a measure for number of cycles still to fill with gates in the schedule,
    // and are coined 'remaining' cycles here.
    //
    // remaining[node] indicates number of cycles remaining in schedule after start execution of node;
    //
    // Please note that for forward (backward) scheduling we use an
    // adaptation of the ALAP (ASAP) cycle computation to compute the remaining values; with this
    // definition both in forward and backward scheduling, a higher remaining indicates more criticality.
    // This means that criticality has become independent of the direction of scheduling
    // which is easier in the core of the scheduler.

    // Note that set_remaining_gate expects a caller like set_remaining that iterates gp backward over the circuit
    void set_remaining_gate(ql::gate* gp, ql::scheduling_direction_t dir)
    {
        ListDigraph::Node   currNode = node[gp];
        size_t              currRemain = 0;
        if (ql::forward_scheduling == dir)
        {
            for( ListDigraph::OutArcIt arc(graph,currNode); arc != INVALID; ++arc )
            {
                currRemain = std::max(currRemain, remaining[graph.target(arc)] + weight[arc]);
            }
        }
        else
        {
            for( ListDigraph::InArcIt arc(graph,currNode); arc != INVALID; ++arc )
            {
                currRemain = std::max(currRemain, remaining[graph.source(arc)] + weight[arc]);
            }
        }
        remaining[currNode] = currRemain;
    }

    void set_remaining(ql::scheduling_direction_t dir)
    {
        ql::gate*   gp;
        remaining.clear();
        if (ql::forward_scheduling == dir)
        {
            // remaining until SINK (i.e. the SINK.cycle-ALAP value)
            remaining[t] = 0;
            // *circp is by definition in a topological order of the dependence graph
            for ( ql::circuit::reverse_iterator gpit = circp->rbegin(); gpit != circp->rend(); gpit++)
            {
                ql::gate*   gp2 = *gpit;
                set_remaining_gate(gp2, dir);
                DOUT("... remaining at " << gp2->qasm() << " cycles " << remaining[node[gp2]]);
            }
            gp = instruction[s];
            set_remaining_gate(gp, dir);
            DOUT("... remaining at " << gp->qasm() << " cycles " << remaining[s]);
        }
        else
        {
            // remaining until SOURCE (i.e. the ASAP value)
            remaining[s] = 0;
            // *circp is by definition in a topological order of the dependence graph
            for ( ql::circuit::iterator gpit = circp->begin(); gpit != circp->end(); gpit++)
            {
                ql::gate*   gp2 = *gpit;
                set_remaining_gate(gp2, dir);
                DOUT("... remaining at " << gp2->qasm() << " cycles " << remaining[node[gp2]]);
            }
            gp = instruction[t];
            set_remaining_gate(gp, dir);
            DOUT("... remaining at " << gp->qasm() << " cycles " << remaining[t]);
        }
    }

    // ASAP/ALAP list scheduling support code with RC
    // Uses an "available list" (avlist) as interface between dependence graph and scheduler
    // the avlist contains all nodes that wrt their dependences can be scheduled:
    // when forward scheduling:
    //  all its predecessors were scheduled
    // when backward scheduling:
    //  all its successors were scheduled
    // The scheduler fills cycles one by one, with nodes/instructions from the avlist
    // checking before selection whether the nodes/instructions have completed execution
    // and whether the resource constraints are fulfilled.

    // Initialize avlist to the single starting node
    // when forward scheduling:
    //  node s (with SOURCE instruction) is the top of the dependence graph; all instructions depend on it
    // when backward scheduling:
    //  node t (with SINK instruction) is the bottom of the dependence graph; it depends on all instructions

    // Set the curr_cycle of the scheduling algorithm to start at the appropriate end as well;
    // note that the cycle attributes will be shifted down to start at 1 after backward scheduling.
    void InitAvailable(std::list<ListDigraph::Node>& avlist, ql::scheduling_direction_t dir, size_t& curr_cycle)
    {
        avlist.clear();
        if (ql::forward_scheduling == dir)
        {
            curr_cycle = 0;
            instruction[s]->cycle = curr_cycle;
            avlist.push_back(s);
        }
        else
        {
            curr_cycle = ALAP_SINK_CYCLE;
            instruction[t]->cycle = curr_cycle;
            avlist.push_back(t);
        }
    }

    // collect the list of directly depending nodes
    // (i.e. those necessarily scheduled after the given node) without duplicates;
    // dependences that are duplicates from the perspective of the scheduler
    // may be present in the dependence graph because the scheduler ignores dependence type and cause
    void get_depending_nodes(ListDigraph::Node n, ql::scheduling_direction_t dir, std::list<ListDigraph::Node> & ln)
    {
        if (ql::forward_scheduling == dir)
        {
            for (ListDigraph::OutArcIt succArc(graph,n); succArc != INVALID; ++succArc)
            {
                ListDigraph::Node succNode = graph.target(succArc);
                // DOUT("...... succ of " << instruction[n]->qasm() << " : " << instruction[succNode]->qasm());
                bool found = false;             // filter out duplicates
                for ( auto anySuccNode : ln )
                {
                    if (succNode == anySuccNode)
                    {
                        // DOUT("...... duplicate: " << instruction[succNode]->qasm());
                        found = true;           // duplicate found
                    }
                }
                if (found == false)             // found new one
                {
                    ln.push_back(succNode);     // new node to ln
                }
            }
            // ln contains depending nodes of n without duplicates
        }
        else
        {
            for (ListDigraph::InArcIt predArc(graph,n); predArc != INVALID; ++predArc)
            {
                ListDigraph::Node predNode = graph.source(predArc);
                // DOUT("...... pred of " << instruction[n]->qasm() << " : " << instruction[predNode]->qasm());
                bool found = false;             // filter out duplicates
                for ( auto anyPredNode : ln )
                {
                    if (predNode == anyPredNode)
                    {
                        // DOUT("...... duplicate: " << instruction[predNode]->qasm());
                        found = true;           // duplicate found
                    }
                }
                if (found == false)             // found new one
                {
                    ln.push_back(predNode);     // new node to ln
                }
            }
            // ln contains depending nodes of n without duplicates
        }
    }

    // Compute of two nodes whether the first one is less deep-critical than the second, for the given scheduling direction;
    // criticality of a node is given by its remaining[node] value which is precomputed;
    // deep-criticality takes into account the criticality of depending nodes (in the right direction!);
    // this function is used to order the avlist in an order from highest deep-criticality to lowest deep-criticality;
    // it is the core of the heuristics of the critical path list scheduler.
    bool criticality_lessthan(ListDigraph::Node n1, ListDigraph::Node n2, ql::scheduling_direction_t dir)
    {
        if (n1 == n2) return false;             // because not <

        if (remaining[n1] < remaining[n2]) return true;
        if (remaining[n1] > remaining[n2]) return false;
        // so: remaining[n1] == remaining[n2]

        std::list<ListDigraph::Node>   ln1;
        std::list<ListDigraph::Node>   ln2;

        get_depending_nodes(n1, dir, ln1);
        get_depending_nodes(n2, dir, ln2);
        if (ln2.empty()) return false;          // strictly < only when ln1.empty and ln2.not_empty
        if (ln1.empty()) return true;           // so when both empty, it is equal, so not strictly <, so false
        // so: ln1.non_empty && ln2.non_empty

        ln1.sort([this](const ListDigraph::Node &d1, const ListDigraph::Node &d2) { return remaining[d1] < remaining[d2]; });
        ln2.sort([this](const ListDigraph::Node &d1, const ListDigraph::Node &d2) { return remaining[d1] < remaining[d2]; });

        size_t crit_dep_n1 = remaining[ln1.back()];    // the last of the list is the one with the largest remaining value
        size_t crit_dep_n2 = remaining[ln2.back()];

        if (crit_dep_n1 < crit_dep_n2) return true;
        if (crit_dep_n1 > crit_dep_n2) return false;
        // so: crit_dep_n1 == crit_dep_n2, call this crit_dep

        ln1.remove_if([this,crit_dep_n1](ListDigraph::Node n) { return remaining[n] < crit_dep_n1; });
        ln2.remove_if([this,crit_dep_n2](ListDigraph::Node n) { return remaining[n] < crit_dep_n2; });
        // because both contain element with remaining == crit_dep: ln1.non_empty && ln2.non_empty

        if (ln1.size() < ln2.size()) return true;
        if (ln1.size() > ln2.size()) return false;
        // so: ln1.size() == ln2.size() >= 1

        ln1.sort([this,dir](const ListDigraph::Node &d1, const ListDigraph::Node &d2) { return criticality_lessthan(d1, d2, dir); });
        ln2.sort([this,dir](const ListDigraph::Node &d1, const ListDigraph::Node &d2) { return criticality_lessthan(d1, d2, dir); });
        return criticality_lessthan(ln1.back(), ln2.back(), dir);
    }

    // Make node n available
    // add it to the avlist because the condition for that is fulfilled:
    //  all its predecessors were scheduled (forward scheduling) or
    //  all its successors were scheduled (backward scheduling)
    // update its cycle attribute to reflect these dependences;
    // avlist is initialized with s or t as first element by InitAvailable
    // avlist is kept ordered on deep-criticality, non-increasing (i.e. highest deep-criticality first)
    void MakeAvailable(ListDigraph::Node n, std::list<ListDigraph::Node>& avlist, ql::scheduling_direction_t dir)
    {
        bool    already_in_avlist = false;  // check whether n is already in avlist
                                            // originates from having multiple arcs between pair of nodes
        std::list<ListDigraph::Node>::iterator first_lower_criticality_inp;     // for keeping avlist ordered
        bool    first_lower_criticality_found = false;                          // for keeping avlist ordered

        DOUT(".... making available node " << name[n] << " remaining: " << remaining[n]);
        for (std::list<ListDigraph::Node>::iterator inp = avlist.begin(); inp != avlist.end(); inp++)
        {
            if (*inp == n)
            {
                already_in_avlist = true;
                DOUT("...... duplicate when making available: " << name[n]);
            }
            else
            {
                // scanning avlist from front to back (avlist is ordered from high to low criticality)
                // when encountering first node *inp with less criticality,
                // that is where new node n should be inserted just before
                // to keep avlist in desired order
                if (criticality_lessthan(*inp, n, dir) && !first_lower_criticality_found)
                {
                    first_lower_criticality_inp = inp;
                    first_lower_criticality_found = true;
                }
            }
        }
        if (!already_in_avlist)
        {
            set_cycle_gate(instruction[n], dir);        // for the schedulers to inspect whether gate has completed
            if (first_lower_criticality_found)
            {
                // add n to avlist just before the first with lower criticality
                avlist.insert(first_lower_criticality_inp, n);
            }
            else
            {
                // add n to end of avlist, if none found with less criticality
                avlist.push_back(n);
            }
            DOUT("...... made available node(@" << instruction[n]->cycle << "): " << name[n] << " remaining: " << remaining[n]);
        }
    }

    // take node n out of avlist because it has been scheduled;
    // reflect that the node has been scheduled in the scheduled vector;
    // having scheduled it means that its depending nodes might become available:
    // such a depending node becomes available when all its dependent nodes have been scheduled now
    //
    // i.e. when forward scheduling:
    //   this makes its successor nodes available provided all their predecessors were scheduled;
    //   a successor node which has a predecessor which hasn't been scheduled,
    //   will be checked here at least when that predecessor is scheduled
    // i.e. when backward scheduling:
    //   this makes its predecessor nodes available provided all their successors were scheduled;
    //   a predecessor node which has a successor which hasn't been scheduled,
    //   will be checked here at least when that successor is scheduled
    //
    // update (through MakeAvailable) the cycle attribute of the nodes made available
    // because from then on that value is compared to the curr_cycle to check
    // whether a node has completed execution and thus is available for scheduling in curr_cycle
    void TakeAvailable(ListDigraph::Node n, std::list<ListDigraph::Node>& avlist, ListDigraph::NodeMap<bool> & scheduled, ql::scheduling_direction_t dir)
    {
        scheduled[n] = true;
        avlist.remove(n);

        if (ql::forward_scheduling == dir)
        {
            for (ListDigraph::OutArcIt succArc(graph,n); succArc != INVALID; ++succArc)
            {
                ListDigraph::Node succNode = graph.target(succArc);
                bool schedulable = true;
                for (ListDigraph::InArcIt predArc(graph,succNode); predArc != INVALID; ++predArc)
                {
                    ListDigraph::Node predNode = graph.source(predArc);
                    if (!scheduled[predNode])
                    {
                        schedulable = false;
                        break;
                    }
                }
                if (schedulable)
                {
                    MakeAvailable(succNode, avlist, dir);
                }
            }
        }
        else
        {
            for (ListDigraph::InArcIt predArc(graph,n); predArc != INVALID; ++predArc)
            {
                ListDigraph::Node predNode = graph.source(predArc);
                bool schedulable = true;
                for (ListDigraph::OutArcIt succArc(graph,predNode); succArc != INVALID; ++succArc)
                {
                    ListDigraph::Node succNode = graph.target(succArc);
                    if (!scheduled[succNode])
                    {
                        schedulable = false;
                        break;
                    }
                }
                if (schedulable)
                {
                    MakeAvailable(predNode, avlist, dir);
                }
            }
        }
    }

    // advance curr_cycle
    // when no node was selected from the avlist, advance to the next cycle
    // and try again; this makes nodes/instructions to complete execution for one more cycle,
    // and makes resources finally available in case of resource constrained scheduling
    // so it contributes to proceeding and to finally have an empty avlist
    void AdvanceCurrCycle(ql::scheduling_direction_t dir, size_t& curr_cycle)
    {
        if (ql::forward_scheduling == dir)
        {
            curr_cycle++;
        }
        else
        {
            curr_cycle--;
        }
    }

    // reading platform dependent gate attributes for rc scheduling
    //
    // get the gate parameters that need to be passed to the resource manager;
    // it would have been nicer if they would have been made available by the platform
    // directly to the resource manager since this function makes the mapper dependent on cc_light
    void GetGateParameters(std::string id, const ql::quantum_platform& platform, std::string& operation_name, std::string& operation_type, std::string& instruction_type)
    {
        DOUT("... getting gate parameters of " << id);
        if (platform.instruction_settings.count(id) > 0)
        {
            DOUT("...... extracting operation_name");
	        if ( !platform.instruction_settings[id]["cc_light_instr"].is_null() )
	        {
	            operation_name = platform.instruction_settings[id]["cc_light_instr"];
	        }
            else
            {
	            operation_name = id;
                DOUT("...... faking operation_name to " << operation_name);
            }

            DOUT("...... extracting operation_type");
	        if ( !platform.instruction_settings[id]["type"].is_null() )
	        {
	            operation_type = platform.instruction_settings[id]["type"];
	        }
            else
            {
	            operation_type = "cc_light_type";
                DOUT("...... faking operation_type to " << operation_type);
            }

            DOUT("...... extracting instruction_type");
	        if ( !platform.instruction_settings[id]["cc_light_instr_type"].is_null() )
	        {
	            instruction_type = platform.instruction_settings[id]["cc_light_instr_type"];
	        }
            else
            {
	            instruction_type = "cc_light";
                DOUT("...... faking instruction_type to " << instruction_type);
            }
        }
        else
        {
            DOUT("Error: platform doesn't support gate '" << id << "'");
            EOUT("Error: platform doesn't support gate '" << id << "'");
            throw ql::exception("[x] Error : platform doesn't support gate!",false);
        }
        DOUT("... getting gate parameters [done]");
    }

    // a gate must wait until all its operand are available, i.e. the gates having computed them have completed,
    // and must wait until all resources required for the gate's execution are available;
    // return true when immediately schedulable
    // when returning false, isres indicates whether resource occupation was the reason or operand completion (for debugging)
    bool immediately_schedulable(ListDigraph::Node n, ql::scheduling_direction_t dir, const size_t curr_cycle,
                                const ql::quantum_platform& platform, ql::arch::resource_manager_t& rm, bool& isres)
    {
        ql::gate*   gp = instruction[n];
        isres = true;
        // have dependent gates completed at curr_cycle?
        if (    ( ql::forward_scheduling == dir && gp->cycle <= curr_cycle)
            ||  ( ql::backward_scheduling == dir && curr_cycle <= gp->cycle)
            )
        {
            // are resources available?
            if ( n == s || n == t
                || gp->type() == ql::gate_type_t::__dummy_gate__ 
                || gp->type() == ql::gate_type_t::__classical_gate__ 
               )
            {
                return true;
            }
            std::string operation_name;
            std::string operation_type;
            std::string instruction_type;
            size_t      operation_duration = std::ceil( static_cast<float>(gp->duration) / cycle_time);
            GetGateParameters(gp->name, platform, operation_name, operation_type, instruction_type);
            if (rm.available(curr_cycle, gp, operation_name, operation_type, instruction_type, operation_duration))
            {
                return true;
            }
            isres = true;
            return false;
        }
        else
        {
            isres = false;
            return false;
        }
    }

    // select a node from the avlist
    // the avlist is deep-ordered from high to low criticality (see criticality_lessthan above)
    ListDigraph::Node SelectAvailable(std::list<ListDigraph::Node>& avlist, ql::scheduling_direction_t dir, const size_t curr_cycle,
                                const ql::quantum_platform& platform, ql::arch::resource_manager_t& rm, bool & success)
    {
        success = false;                        // whether a node was found and returned
        
        DOUT("avlist(@" << curr_cycle << "):");
        for ( auto n : avlist)
        {
            DOUT("...... node(@" << instruction[n]->cycle << "): " << name[n] << " remaining: " << remaining[n]);
        }

        // select the first immediately schedulable, if any
        // since avlist is deep-criticality ordered, highest first, the first is the most deep-critical
        for ( auto n : avlist)
        {
            bool isres;
            if ( immediately_schedulable(n, dir, curr_cycle, platform, rm, isres) )
            {
                DOUT("... node (@" << instruction[n]->cycle << "): " << name[n] << " immediately schedulable, remaining=" << remaining[n] << ", selected");
                success = true;
                return n;
            }
            else
            {
                DOUT("... node (@" << instruction[n]->cycle << "): " << name[n] << " remaining=" << remaining[n] << ", waiting for " << (isres? "resource" : "dependent completion"));
            }
        }

        success = false;
        return s;   // fake return value
    }
    
    void add_instructon()
    {
        // TODO
    }
    
    bool has_deadlock(size_t curr_cycle, ql::scheduling_direction_t dir,
        std::list<ListDigraph::Node>& avlist, ListDigraph::NodeMap<bool>& scheduled)
    {
        // get minimum duration of all available instructions
        size_t min_duration = ALAP_SINK_CYCLE;
        for (const auto& n : avlist)
        {
            ql::gate* ins = instruction[n];
            if (ins->duration < min_duration)
            {
                min_duration = ins->duration;
            }
        }
        
        // get the number of scheduled instructions executing in current cycle
        int executing_ins = 0;
        for (const auto& entry : node)
        {
            ql::gate* ins = entry.first;
            ListDigraph::Node n = entry.second;
            size_t op_start_cycle = ins->cycle;
            size_t operation_duration = std::ceil(static_cast<float>(ins->duration) / cycle_time);
            
            // count SOURCE as instruction
            if ((n == s || ins->type() != ql::gate_type_t::__dummy_gate__)
                && ins->type() != ql::gate_type_t::__classical_gate__ 
                && scheduled[n]
                && curr_cycle < (op_start_cycle + operation_duration) && op_start_cycle < (curr_cycle + min_duration))
            {
                executing_ins++;
            }
        }
        
        // get the number of not schedulable instructions due to resources
        int problematic_ins = 0;
        for (const auto& n : avlist)
        {
            ql::gate* ins = instruction[n];
            if ((ql::forward_scheduling == dir && ins->cycle <= curr_cycle)
                || (ql::backward_scheduling == dir && curr_cycle <= ins->cycle)
            )
            {
                problematic_ins++;
            }
        }
        
        return (problematic_ins > 0 && executing_ins == 0);
    }
    
    void solve_deadlock(size_t  curr_cycle, ListDigraph::Node n,
        const ql::quantum_platform& platform, ql::arch::resource_manager_t& rm)
    {
        ql::gate* gp = instruction[n];
        
        if (n != s && n != t
            && gp->type() != ql::gate_type_t::__dummy_gate__ 
            && gp->type() != ql::gate_type_t::__classical_gate__ 
           )
        {
            std::string operation_name;
            std::string operation_type;
            std::string instruction_type;
            size_t operation_duration = std::ceil(static_cast<float>(gp->duration) / cycle_time);
            GetGateParameters(gp->name, platform, operation_name, operation_type, instruction_type);

            rm.solve_deadlock(curr_cycle, gp, operation_name, operation_type, instruction_type, operation_duration);
        }
    }
    
    // ASAP/ALAP scheduler with RC
    //
    // schedule the circuit that is in the dependence graph
    // for the given direction, with the given platform and resource manager;
    // what is done, is:
    // - the cycle attribute of the gates will be set according to the scheduling method
    // - *circp (the original and result circuit) is sorted in the new cycle order
    // - bundles are collected from the circuit
    // - latency compensation and buffer-buffer delay insertion done
    // the bundles are returned, with private start/duration attributes
    ql::ir::bundles_t schedule_post179(ql::circuit* circp, ql::scheduling_direction_t dir,
            const ql::quantum_platform& platform, ql::arch::resource_manager_t& rm)
    {
        DOUT("Scheduling " << (ql::forward_scheduling == dir?"ASAP":"ALAP") << " with RC ...");

        // scheduled[n] :=: whether node n has been scheduled, init all false
        ListDigraph::NodeMap<bool>      scheduled(graph);
        // avlist :=: list of schedulable nodes, initially (see below) just s or t
        std::list<ListDigraph::Node>    avlist;

        // initializations for this scheduler
        // note that dependence graph is not modified by a scheduler, so it can be reused
        DOUT("... initialization");
        for (ListDigraph::NodeIt n(graph); n != INVALID; ++n)
        {
            scheduled[n] = false;   // none were scheduled
        }
        size_t  curr_cycle;         // current cycle for which instructions are sought
        InitAvailable(avlist, dir, curr_cycle);     // first node (SOURCE/SINK) is made available and curr_cycle set
        set_remaining(dir);         // for each gate, number of cycles until end of schedule
        
        DOUT("... loop over avlist until it is empty");
        while (!avlist.empty())
        {
            bool success;
            ListDigraph::Node   selected_node;
            
            DOUT(std::string(std::string("Curr cycle ") + std::to_string(curr_cycle)).c_str());
            
            selected_node = SelectAvailable(avlist, dir, curr_cycle, platform, rm, success);
            if (!success)
            {
                // check for deadlock
                if (has_deadlock(curr_cycle, dir, avlist, scheduled))
                {
                    // try to solve deadlock with any instruction
                    ListDigraph::Node n = avlist.front();
                    solve_deadlock(curr_cycle, n, platform, rm);
                    
                    // Test if it worked
                    SelectAvailable(avlist, dir, curr_cycle, platform, rm, success);
                    if (!success)
                    {
                        EOUT("Can not solve deadlock. Exiting.");
                        exit(1);
                    }
                    else
                    {
                        // retry scheduling this cycle
                        continue;
                    }
                }
                
                DOUT("Next cycle");
                
                // i.e. none from avlist was found suitable to schedule in this cycle
                AdvanceCurrCycle(dir, curr_cycle); 
                // so try again; eventually instrs complete and machine is empty
                continue;
            }

            // commit selected_node to the schedule
            ql::gate* gp = instruction[selected_node];
            DOUT("... selected " << gp->qasm() << " in cycle " << curr_cycle);
            gp->cycle = curr_cycle;                     // scheduler result, including s and t
            if (selected_node != s
                && selected_node != t
                && gp->type() != ql::gate_type_t::__dummy_gate__ 
                && gp->type() != ql::gate_type_t::__classical_gate__ 
               )
            {
                std::string operation_name;
                std::string operation_type;
                std::string instruction_type;
                size_t      operation_duration = 0;

                GetGateParameters(gp->name, platform, operation_name, operation_type, instruction_type);
                operation_duration = std::ceil( static_cast<float>(gp->duration) / cycle_time);
                rm.reserve(curr_cycle, gp, operation_name, operation_type, instruction_type, operation_duration);
            }
            TakeAvailable(selected_node, avlist, scheduled, dir);   // update avlist/scheduled/cycle
            // more nodes that could be scheduled in this cycle, will be found in an other round of the loop
        }

        DOUT("... sorting on cycle value");
        sort_by_cycle();

        if (ql::backward_scheduling == dir)
        {
            // readjust cycle values of gates so that SOURCE is at 0
            size_t  SOURCECycle = instruction[s]->cycle;
            DOUT("... readjusting cycle values by -" << SOURCECycle);

            instruction[t]->cycle -= SOURCECycle;
            for ( auto & gp : *circp)
            {
                gp->cycle -= SOURCECycle;
            }
            instruction[s]->cycle -= SOURCECycle;   // i.e. becomes 0
        }

        latency_compensation(circp, platform);

        ql::ir::bundles_t   bundles;
        bundles = bundler(*circp);

        insert_buffer_delays(bundles, platform);

        DOUT("Scheduling " << (ql::forward_scheduling == dir?"ASAP":"ALAP") << " with RC [DONE]");
        return bundles;
    }

    ql::ir::bundles_t schedule_asap_post179(ql::arch::resource_manager_t & rm, const ql::quantum_platform & platform)
    {
        ql::ir::bundles_t   bundles;
        bundles = schedule_post179(circp, ql::forward_scheduling, platform, rm);

        DOUT("Scheduling ASAP [DONE]");
        return bundles;
    }

    ql::ir::bundles_t schedule_alap_post179(ql::arch::resource_manager_t & rm, const ql::quantum_platform & platform)
    {
        ql::ir::bundles_t   bundles;
        bundles = schedule_post179(circp, ql::backward_scheduling, platform, rm);

        DOUT("Scheduling ALAP [DONE]");
        return bundles;
    }

// =========== post179 uniform
    ql::ir::bundles_t schedule_alap_uniform_post179()
    {
        // algorithm based on "Balanced Scheduling and Operation Chaining in High-Level Synthesis for FPGA Designs"
        // by David C. Zaretsky, Gaurav Mittal, Robert P. Dick, and Prith Banerjee
        // Figure 3. Balanced scheduling algorithm
        // Modifications:
        // - dependency analysis in article figure 2 is O(n^2) because of set union
        //   this has been left out, using our own linear dependency analysis creating a digraph
        //   and using the alap values as measure instead of the dep set size computed in article's D[n]
        // - balanced scheduling algorithm dominates with its O(n^2) when it cannot find a node to forward
        //   no test has been devised yet to break the loop (figure 3, line 14-35)
        // - targeted bundle size is adjusted each cycle and is number_of_gates_to_go/number_of_non_empty_bundles_to_go
        //   this is more greedy, preventing oscillation around a target size based on all bundles,
        //   because local variations caused by local dep chains create small bundles and thus leave more gates still to go
        //
        // Oddly enough, it starts off with an ASAP schedule.
        // This creates bundles which on average are larger at lower cycle values (opposite to ALAP).
        // After this, it moves gates up in the direction of the higher cycles but, of course, at most to their ALAP cycle
        // to fill up the small bundles at the higher cycle values to the targeted uniform length, without extending the circuit.
        // It does this in a backward scan (as ALAP scheduling would do), so bundles at the highest cycles are filled up first,
        // and such that the circuit's depth is not enlarged and the dependences/latencies are obeyed.
        // Hence, the result resembles an ALAP schedule with excess bundle lengths solved by moving nodes down ("rolling pin").

        DOUT("Scheduling ALAP UNIFORM to get bundles ...");
        ql::ir::bundles_t bundles;

        // initialize gp->cycle as ASAP cycles as first approximation of result;
        // note that the circuit doesn't contain the SOURCE and SINK gates but the dependence graph does;
        // from SOURCE is a weight 1 dep to the first nodes using each qubit and classical register, and to the SINK gate
        // is a dep from each unused qubit/classical register result with as weight the duration of the last operation.
        // SOURCE (node s) is at cycle 0 and the first circuit's gates are at cycle 1.
        // SINK (node t) is at the earliest cycle that all gates/operations have completed.
        set_cycle(ql::forward_scheduling);
        size_t   cycle_count = instruction[t]->cycle - 1;
        // so SOURCE at cycle 0, then all circuit's gates at cycles 1 to cycle_count, and finally SINK at cycle cycle_count+1

        // compute remaining which is the opposite of the alap cycle value (remaining[node] :=: SINK->cycle - alapcycle[node])
        // remaining[node] indicates number of cycles remaining in schedule from node's execution start to SINK,
        // and indicates the latest cycle that the node can be scheduled so that the circuit's depth is not increased.
        set_remaining(ql::forward_scheduling);

        // DOUT("Creating gates_per_cycle");
        // create gates_per_cycle[cycle] = for each cycle the list of gates at cycle cycle
        // this is the basic map to be operated upon by the uniforming scheduler below;
        std::map<size_t,std::list<ql::gate*>> gates_per_cycle;
        for ( ql::circuit::iterator gpit = circp->begin(); gpit != circp->end(); gpit++)
        {
            ql::gate*           gp = *gpit;
            gates_per_cycle[gp->cycle].push_back(gp);
        }

        // DOUT("Displaying circuit and bundle statistics");
        // to compute how well the algorithm is doing, two measures are computed:
        // - the largest number of gates in a cycle in the circuit,
        // - and the average number of gates in non-empty cycles
        // this is done before and after uniform scheduling, and printed
        size_t max_gates_per_cycle = 0;
        size_t non_empty_bundle_count = 0;
        size_t gate_count = 0;
        for (size_t curr_cycle = 1; curr_cycle <= cycle_count; curr_cycle++)
        {
            max_gates_per_cycle = std::max(max_gates_per_cycle, gates_per_cycle[curr_cycle].size());
            if (int(gates_per_cycle[curr_cycle].size()) != 0) non_empty_bundle_count++;
            gate_count += gates_per_cycle[curr_cycle].size();
        }
        double avg_gates_per_cycle = double(gate_count)/cycle_count;
        double avg_gates_per_non_empty_cycle = double(gate_count)/non_empty_bundle_count;
        DOUT("... before uniform scheduling:"
            << " cycle_count=" << cycle_count
            << "; gate_count=" << gate_count
            << "; non_empty_bundle_count=" << non_empty_bundle_count
            );
        DOUT("... and max_gates_per_cycle=" << max_gates_per_cycle
            << "; avg_gates_per_cycle=" << avg_gates_per_cycle
            << "; avg_gates_per_non_empty_cycle=" << avg_gates_per_non_empty_cycle
            );

        // in a backward scan, make non-empty bundles max avg_gates_per_non_empty_cycle long;
        // an earlier version of the algorithm aimed at making bundles max avg_gates_per_cycle long
        // but that flawed because of frequent empty bundles causing this estimate for a uniform length being too low
        // DOUT("Backward scan uniform scheduling");
        for (size_t curr_cycle = cycle_count; curr_cycle >= 1; curr_cycle--)
        {
            // Backward with pred_cycle from curr_cycle-1 down to 1, look for node(s) to fill up current too small bundle.
            // After an iteration at cycle curr_cycle, all bundles from curr_cycle to cycle_count have been filled up,
            // and all bundles from 1 to curr_cycle-1 still have to be done.
            // This assumes that current bundle is never too long, excess having been moved away earlier, as ASAP does.
            // When such a node cannot be found, this loop scans the whole circuit for each original node to fill up
            // and this creates a O(n^2) time complexity.
            //
            // A test to break this prematurely based on the current data structure, wasn't devised yet.
            // A solution is to use the dep graph instead to find a node to fill up the current node,
            // i.e. maintain a so-called "available list" of nodes free to schedule, as in the non-uniform scheduling algorithm,
            // which is not hard at all but which is not according to the published algorithm.
            // When the complexity becomes a problem, it is proposed to rewrite the algorithm accordingly.

            long pred_cycle = curr_cycle - 1;    // signed because can become negative

            // target size of each bundle is number of gates still to go divided by number of non-empty cycles to go
            // it averages over non-empty bundles instead of all bundles because the latter would be very strict
            // it is readjusted during the scan to cater for dips in bundle size caused by local dependence chains
            if (non_empty_bundle_count == 0) break;     // nothing to do
            avg_gates_per_cycle = double(gate_count)/curr_cycle;
            avg_gates_per_non_empty_cycle = double(gate_count)/non_empty_bundle_count;
            DOUT("Cycle=" << curr_cycle << " number of gates=" << gates_per_cycle[curr_cycle].size()
                << "; avg_gates_per_cycle=" << avg_gates_per_cycle
                << "; avg_gates_per_non_empty_cycle=" << avg_gates_per_non_empty_cycle);

            while ( double(gates_per_cycle[curr_cycle].size()) < avg_gates_per_non_empty_cycle && pred_cycle >= 1 )
            {
                DOUT("pred_cycle=" << pred_cycle);
                DOUT("gates_per_cycle[curr_cycle].size()=" << gates_per_cycle[curr_cycle].size());
                size_t      min_remaining_cycle = MAX_CYCLE;
                ql::gate*   best_predgp;
                bool        best_predgp_found = false;

                // scan bundle at pred_cycle to find suitable candidate to move forward to curr_cycle
                for ( auto predgp : gates_per_cycle[pred_cycle] )
                {
                    bool    forward_predgp = true;
                    size_t  predgp_completion_cycle;
                    ListDigraph::Node   pred_node = node[predgp];
                    DOUT("... considering: " << predgp->qasm() << " @cycle=" << predgp->cycle << " remaining=" << remaining[pred_node]);

                    // candidate's result, when moved, must be ready before end-of-circuit and before used
                    predgp_completion_cycle = curr_cycle + std::ceil(static_cast<float>(predgp->duration)/cycle_time);
                    if (predgp_completion_cycle > cycle_count + 1)  // at SINK is ok, later not
                    {
                        forward_predgp = false;
                        DOUT("... ... rejected (after circuit): " << predgp->qasm() << " would complete @" << predgp_completion_cycle << " SINK @" << cycle_count+1);
                    }
                    else
                    {
                        for ( ListDigraph::OutArcIt arc(graph,pred_node); arc != INVALID; ++arc )
                        {
                            ql::gate*   target_gp = instruction[graph.target(arc)];
                            size_t target_cycle = target_gp->cycle;
                            if(predgp_completion_cycle > target_cycle)
                            {
                                forward_predgp = false;
                                DOUT("... ... rejected (after succ): " << predgp->qasm() << " would complete @" << predgp_completion_cycle << " target=" << target_gp->qasm() << " target_cycle=" << target_cycle);
                            }
                        }
                    }

                    // when multiple nodes in bundle qualify, take the one with lowest remaining
                    // because that is the most critical one and thus deserves a cycle as high as possible (ALAP)
                    if (forward_predgp && remaining[pred_node] < min_remaining_cycle)
                    {
                        min_remaining_cycle = remaining[pred_node];
                        best_predgp_found = true;
                        best_predgp = predgp;
                    }
                }

                // when candidate was found in this bundle, move it, and search for more in this bundle, if needed
                // otherwise, continue scanning backward
                if (best_predgp_found)
                {
                    // move predgp from pred_cycle to curr_cycle;
                    // adjust all bookkeeping that is affected by this
                    gates_per_cycle[pred_cycle].remove(best_predgp);
                    if (gates_per_cycle[pred_cycle].size() == 0)
                    {
                        // source bundle was non-empty, now it is empty
                        non_empty_bundle_count--;
                    }
                    if (gates_per_cycle[curr_cycle].size() == 0)
                    {
                        // target bundle was empty, now it will be non_empty
                        non_empty_bundle_count++;
                    }
                    best_predgp->cycle = curr_cycle;        // what it is all about
                    gates_per_cycle[curr_cycle].push_back(best_predgp);
                   
                    // recompute targets
                    if (non_empty_bundle_count == 0) break;     // nothing to do
                    avg_gates_per_cycle = double(gate_count)/curr_cycle;
                    avg_gates_per_non_empty_cycle = double(gate_count)/non_empty_bundle_count;
                    DOUT("... moved " << best_predgp->qasm() << " with remaining=" << remaining[node[best_predgp]]
                        << " from cycle=" << pred_cycle << " to cycle=" << curr_cycle
                        << "; new avg_gates_per_cycle=" << avg_gates_per_cycle
                        << "; avg_gates_per_non_empty_cycle=" << avg_gates_per_non_empty_cycle
                        );
                }
                else
                {
                    pred_cycle --;
                }
            }   // end for finding a bundle to forward a node from to the current cycle

            // curr_cycle ready, recompute counts for remaining cycles
            // mask current cycle and its gates from the target counts:
            // - gate_count, non_empty_bundle_count, curr_cycle (as cycles still to go)
            gate_count -= gates_per_cycle[curr_cycle].size();
            if (gates_per_cycle[curr_cycle].size() != 0)
            {
                // bundle is non-empty
                non_empty_bundle_count--;
            }
        }   // end curr_cycle loop; curr_cycle is bundle which must be enlarged when too small

        // new cycle values computed; reflect this in circuit's gate order
        sort_by_cycle();

        // recompute and print statistics reporting on uniform scheduling performance
        max_gates_per_cycle = 0;
        non_empty_bundle_count = 0;
        gate_count = 0;
        // cycle_count was not changed
        for (size_t curr_cycle = 1; curr_cycle <= cycle_count; curr_cycle++)
        {
            max_gates_per_cycle = std::max(max_gates_per_cycle, gates_per_cycle[curr_cycle].size());
            if (int(gates_per_cycle[curr_cycle].size()) != 0) non_empty_bundle_count++;
            gate_count += gates_per_cycle[curr_cycle].size();
        }
        avg_gates_per_cycle = double(gate_count)/cycle_count;
        avg_gates_per_non_empty_cycle = double(gate_count)/non_empty_bundle_count;
        DOUT("... after uniform scheduling:"
            << " cycle_count=" << cycle_count
            << "; gate_count=" << gate_count
            << "; non_empty_bundle_count=" << non_empty_bundle_count
            );
        DOUT("... and max_gates_per_cycle=" << max_gates_per_cycle
            << "; avg_gates_per_cycle=" << avg_gates_per_cycle
            << "; ..._per_non_empty_cycle=" << avg_gates_per_non_empty_cycle
            );

        // prefer standard bundler over using the gates_per_cycle data structure
        bundles = bundler(*circp);

        DOUT("Scheduling ALAP UNIFORM to get bundles [DONE]");
        return bundles;
    }

public:

// =========== scheduling entry points switching out to pre179 or post179

    ql::ir::bundles_t schedule_asap()
    {
        if (ql::options::get("scheduler_post179") == "no")
        {
            return schedule_asap_pre179();
        }
        else
        {
            return schedule_asap_post179();
        }
    }

    ql::ir::bundles_t schedule_asap(ql::arch::resource_manager_t & rm, const ql::quantum_platform & platform)
    {
        if (ql::options::get("scheduler_post179") == "no")
        {
            return schedule_asap_pre179(rm, platform);
        }
        else
        {
            return schedule_asap_post179(rm, platform);
        }
    }

    ql::ir::bundles_t schedule_alap()
    {
        if (ql::options::get("scheduler_post179") == "no")
        {
            return schedule_alap_pre179();
        }
        else
        {
            return schedule_alap_post179();
        }
    }

    ql::ir::bundles_t schedule_alap(ql::arch::resource_manager_t & rm, const ql::quantum_platform & platform)
    {
        if (ql::options::get("scheduler_post179") == "no")
        {
            return schedule_alap_pre179(rm, platform);
        }
        else
        {
            return schedule_alap_post179(rm, platform);
        }
    }

    ql::ir::bundles_t schedule_alap_uniform()
    {
        if (ql::options::get("scheduler_post179") == "no")
        {
            return schedule_alap_uniform_pre179();
        }
        else
        {
            return schedule_alap_uniform_post179();
        }
    }
};

#endif

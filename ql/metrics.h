/**
 * @file   metrics.h
 * @date   04/2019 - now
 * @author Diogo Valada
 * @brief  OpenQl circuit success rate estimator
 */

#pragma once
#include <cmath> 
#include <vector>
#include "gate.h"

namespace ql
{
// class Metrics {

// private:
// 	size_t Nqubits;
// 	double gatefid_1;
// 	double gatefid_2; 
// 	double decoherence_time;

// public:

// 	Metrics(size_t Nqubits, double gatefid_1, double gatefid_2, double decoherence_time) : Nqubits(Nqubits), gatefid_1(gatefid_1), gatefid_2(gatefid_2), decoherence_time(decoherence_time)
// 	{
// 	}
// };



double bounded_fidelity(ql::circuit& circ, size_t Nqubits, double gatefid_1, double gatefid_2, double decoherence_time, std::vector<double> &fids = std::vector<double>(), std::string output_mode="worst")
{ 
    //this function considers the primitive gates! each operand undergoing a 2-qubit operation is always considered to have the same latency
	//same end fidelity considered for the two operands of the same 2-qubit gate
	//The output is the worst fidelity from the all the qubits (output_mode = "worst")
	//TODO - Gate error rate not considered
	//TODO - Exp operation can be optimized. Exponential of k cycles is the same as each cycle^k
	//TODO - URGENT!!!! Confirm whether gate.duration is defined in cycles or ns!!! Considering cycles currently
	


	if (fids.size() == 0)
	{
		fids.reserve(Nqubits);
		std::fill_n(fids, Nqubits, 1.0); //Initiallize a fidelity vector, if one is not provided
	}

	std::vector<size_t> last_op_endtime(Nqubits, 0);

	for (auto gate : circ)
	{

		if (gate->name == "measure")
			continue;
		
		unsigned char type_op = gate->operands.size(); // type of operation (1-qubit/2-qubit)
		if (type_op == 1)
		{
			size_t qubit = gate->operands[0];
			size_t last_time = last_op_endtime[qubit];
			size_t idled_time = gate->cycle - last_time; //get idlying time to introduce decoherence. This assumes "cycle" starts at zero, otherwise gate->cycle-> (gate->cycle - 1)
			last_op_endtime[qubit] = gate->cycle  + gate->duration; //This assumes "cycle" starts at zero, otherwise gate->cycle-> (gate->cycle - 1)
			
			fids[qubit] *= std::exp(-decoherence_time*idled_time); // Update fidelity with idling-caused decoherence
			fids[qubit] *= gatefid_1; //Update fidelity after gate
		}
		else if (type_op == 2)
		{
			size_t qubit_c = gate->operands[0];
			size_t qubit_t = gate->operands[1];
			
			size_t last_time_c = last_op_endtime[qubit_c];
			size_t last_time_t = last_op_endtime[qubit_t];
			size_t idled_time_c = gate->cycle - last_time_c;
			size_t idled_time_t = gate->cycle - last_time_t; //get idlying time to introduce decoherence. This assumes "cycle" starts at zero, otherwise gate->cycle-> (gate->cycle - 1)
			last_op_endtime[qubit_c] = gate->cycle  + gate->duration; //This assumes "cycle" starts at zero, otherwise gate->cycle-> (gate->cycle - 1)
			last_op_endtime[qubit_t] = gate->cycle  + gate->duration; //This assumes "cycle" starts at zero, otherwise gate->cycle-> (gate->cycle - 1)
			
			fids[qubit_c] *= std::exp(-decoherence_time*idled_time_c); // Update fidelity with idling-caused decoherence
			fids[qubit_t] *= std::exp(-decoherence_time*idled_time_t); // Update fidelity with idling-caused decoherence


			fids[qubit_c] *=  fids[qubit_t] * gatefid_2; //Update fidelity after gate
			fids[qubit_t] = fids[qubit_c];  					//Update fidelity after gate

			//TODO - Convert the code into a for loop with range 2, to get the compiler's for optimization (and possible paralellization?)
		}
	}
	if (output_mode == "worst")
		return *std::min_element(fids.begin(),fids.end());
	// else if (output_mode == "gaussian")
	// {
	// 	return 0 ;
	// }

	//TODO - substitute for a better output? Consider all the fidelities, but still give a bigger weight to the lowest ones.
	//TODO - Test the different possibilities and find the most accurate one.

};

// double heuristic_metric()
// {
// 	//Maybe try to understand how the fidelity of each operand evolves
// 	//Extra: as a function of the initial and final qubit fidelity
// 	return 0;
// };
















} //namespace end

# Crossbar Architecture

This crossbar architecture is based on the paper [A crossbar network for silicon quantum dot qubits](https://arxiv.org/pdf/1711.03807.pdf).

## Config Structure

This archiecture has the following structure:

### Topology

- Grid matrix

### Resources

- Qubits
- Barriers
- Qubit Lines
- Wave

### Decomposition

### Supported Gates

#### Shuttling
- shuttle_up
- shuttle_down
- shuttle_left
- shuttle_right

#### Single Qubit Gates
- prep_x
- prep_y
- prep_z
- x
- y
- z
- s
- s_dag
- t
- t_dag

#### Z Gate
- z (normal global execution)
- z_shuttle_left (shuttling execution)
- z_shuttle_right (shuttling execution)

#### Two Qubit Gates
- sqswap
- cphase (not implemented)

#### Measurement
- measurement_left_up
- measurement_left_down
- measurement_right_up
- measurement_right_down

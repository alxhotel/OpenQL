import os
from openql import openql as ql

# Set directories
curdir = os.path.dirname(__file__)
output_dir = os.path.join(curdir, 'output')

# Compiler parameters
ql.set_option('output_dir', output_dir)
ql.set_option('optimize', 'no')
ql.set_option('scheduler', 'ASAP')
ql.set_option('log_level', 'LOG_DEBUG')

num_qubits = 2
num_creg = 0
config = os.path.join(curdir, 'hardware_config_crossbar.json')

# Create Platform & Program & Kernel
platform = ql.Platform('crossbar', config)
program = ql.Program('crossbar_test', platform, num_qubits, num_creg)
kernel = ql.Kernel("test", platform, num_qubits, num_creg)

kernel.gate("pepez", [0])
kernel.gate("prepz", [1])

kernel.gate("x", [0])
kernel.gate("cnot", [0, 1])

kernel.gate("shuttle_up", [1])

kernel.gate("measure", [1])

program.add_kernel(kernel)
program.compile()

# qasm = program.qasm()
# print(qasm)

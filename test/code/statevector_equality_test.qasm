qreg q[3];
creg c[3];
h q[0];
cx q[0], q[1];
cx q[0], q[2];
assert-eq q[0], q[1], q[2] { 0.70710678, 0, 0, 0, 0, 0, 0, 0.70710678 } 0.9;

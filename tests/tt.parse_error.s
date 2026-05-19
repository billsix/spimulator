# SPIM S20 MIPS simulator.
# Test that a deliberate parse error causes a non-zero shell
# exit status.
#
# Historical regression: spim would print "spim: (parser)
# syntax error..." but main returned spim_return_value = 0,
# so a Makefile/CI couldn't tell that the build failed.
#
# Driver (from a shell, not from this file):
#
#   $ spimulator -f tt.parse_error.s > /dev/null 2>&1 ; echo $?
#   2
#
# The garbage below is not valid MIPS asm.

this is not valid mips assembly

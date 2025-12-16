# Tests for ffe

AM_CFLAGS = -I$(top_srcdir)/src
AM_CPPFLAGS = -I$(top_srcdir)/src $(LIBGCRYPT_CFLAGS)
LDADD = $(LIBGCRYPT_LIBS)
TESTS_ENVIRONMENT = FFE_BIN='$(FFE_BIN)' srcdir='$(srcdir)'

# Path to the ffe binary
FFE_BIN = $(top_builddir)/src/ffe

# Test scripts (shell scripts)
TESTS = run_tests.sh

# Ensure ffe binary is built before tests run
check-local: $(FFE_BIN)

$(FFE_BIN):
	cd $(top_builddir)/src && $(MAKE) ffe

clean-local:
	rm -f *.log

# Distribute all test files including subdirectories
EXTRA_DIST = run_tests.sh \
	fixed_length/fixed_length.fferc \
	fixed_length/fixed_length.input \
	fixed_length/fixed_length.expected \
	fixed_length/test_fixed_length.sh \
	separated/separated.fferc \
	separated/separated.input \
	separated/separated.expected \
	separated/test_separated.sh \
	binary/binary.fferc \
	binary/binary.input \
	binary/binary.expected \
	binary/test_binary.sh \
	expressions/expression.expected \
	expressions/fixed_length.fferc \
	expressions/fixed_length.input \
	expressions/test_expressions.sh
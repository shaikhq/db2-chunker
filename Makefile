# ---------------------------------------------------------------------------
# Environment (override on the command line, e.g. `make lib DB2PATH=/opt/...`)
# Keep values free of trailing comments: Make would fold the spaces into them.
# ---------------------------------------------------------------------------
DB2PATH   ?= /opt/ibm/db2/V12.1
# ^ Db2 install root; must contain include/sqludf.h
INSTHOME  ?= /home/db2inst1
# ^ Db2 instance home
FUNCDIR   ?= $(INSTHOME)/sqllib/function
# ^ where fenced routine libraries are loaded from
DBNAME    ?= SAMPLE
# ^ database to register/check against
CC        ?= gcc
LIBNAME   ?= db2chunk
# ^ must match the library name in register.sql's EXTERNAL NAME
DEBUG     ?= 1
# ^ 1 => compile the lifecycle trace to stderr (-DCHUNK_DEBUG)

# ---------------------------------------------------------------------------
INCLUDE   := $(DB2PATH)/include
CFLAGS    := -Wall -std=c99 -I$(INCLUDE)
LIBFLAGS  := -fPIC -shared
ifeq ($(DEBUG),1)
CFLAGS    += -DCHUNK_DEBUG
endif

.PHONY: test lib deploy register check clean

# Fast loop: build and run the pure-core test. No Db2 needed.
test:
	$(CC) -Wall -Wextra -std=c99 -o test_core chunk_core.c test_core.c
	./test_core

# Build the fenced shared library from the core + adapter.
lib: $(LIBNAME).so
$(LIBNAME).so: chunk_core.c db2_chunk_udf.c chunk_core.h
	$(CC) $(CFLAGS) $(LIBFLAGS) -o $@ chunk_core.c db2_chunk_udf.c

# Deploy: copy the library into the instance's function directory. Db2 loads
# it by the bare name in EXTERNAL NAME, so drop the .so extension on arrival.
deploy: lib
	cp $(LIBNAME).so $(FUNCDIR)/$(LIBNAME)
	@echo "deployed -> $(FUNCDIR)/$(LIBNAME)"

# Register: create the function in DBNAME. Requires the Db2 environment to be
# sourced (`. $(INSTHOME)/sqllib/db2profile`) and a running instance. The whole
# recipe runs in one shell so the CLP connection persists across commands.
register:
	db2 connect to $(DBNAME) > /dev/null && \
	db2 -tvf register.sql; \
	db2 connect reset > /dev/null

# Check: assert the smoke test returns exactly 3 rows.
check:
	db2 connect to $(DBNAME) > /dev/null && \
	db2 -x "SELECT COUNT(*) FROM TABLE(chunk('chunking is fun to build', 10)) AS t" \
	    | grep -qw 3 && echo "CHECK PASS: 3 rows" || (echo "CHECK FAIL"; exit 1); \
	db2 connect reset > /dev/null

clean:
	rm -f test_core $(LIBNAME).so *.o

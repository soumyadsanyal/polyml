AUTOMAKE_OPTIONS=foreign

moduledir = @moduledir@

AM_CPPFLAGS = $(CFLAGS) $(OSFLAG) $(GIT_VERSION) -Wall -DMODULEDIR=$(moduledir)
AM_CFLAGS = $(CFLAGS) $(OSFLAG) $(GIT_VERSION) -Wall -fno-strict-aliasing
AM_ASFLAGS = $(OSFLAG)
AM_CCASFLAGS = $(OSFLAG)

lib_LTLIBRARIES = libpolyml.la
libpolyml_la_LDFLAGS = -version-info 7:0:0

if INTERNAL_LIBFFI
libpolyml_la_LIBADD = ../libffi/libffi_convenience.la
endif

if NO_UNDEFINED
# Force all references to be defined to build the DLL.
libpolyml_la_LDFLAGS += -no-undefined
endif

# Select the architecture-specific modules
if ARCHI386
ARCHSOURCE = x86_dep.cpp x86asmtemp.S
else
if ARCHINTERPRET
ARCHSOURCE = interpret.cpp
else
if ARCHINTERPRET64
ARCHSOURCE = interpret.cpp
else
if ARCHX86_64
ARCHSOURCE = x86_dep.cpp x86asmtemp.S
else
endif
endif
endif
endif

# Select the object-format-specific modules
if EXPPECOFF
EXPORTSOURCE = pecoffexport.cpp
else
if EXPELF
EXPORTSOURCE = elfexport.cpp
else
if EXPMACHO
EXPORTSOURCE = machoexport.cpp
endif
endif
endif

if NATIVE_WINDOWS
OSSOURCE = Console.cpp windows_specific.cpp
else
OSSOURCE = unix_specific.cpp
endif

noinst_HEADERS = \
	arb.h \
	basicio.h \
	bitmap.h \
	check_objects.h \
	Console.h \
	diagnostics.h \
	elfexport.h \
	errors.h \
	exporter.h \
	foreign.h \
	gc.h \
	gctaskfarm.h \
	globals.h \
        heapsizing.h \
	int_opcodes.h \
	io_internal.h \
	locking.h \
	machine_dep.h \
	machoexport.h \
	memmgr.h \
	mpoly.h \
	network.h \
	noreturn.h \
	objsize.h \
	osmem.h \
	os_specific.h \
	pecoffexport.h \
	pexport.h \
	PolyControl.h \
	poly_specific.h \
	polyffi.h \
	polystring.h \
	process_env.h \
	processes.h \
	profiling.h \
	realconv.h \
	reals.h \
	rts_module.h \
	run_time.h \
	savestate.h \
	save_vec.h \
	scanaddrs.h \
	sharedata.h \
	sighandler.h \
	statistics.h \
	sys.h \
	timing.h \
	version.h \
	xcall_numbers.h \
	xwindows.h

libpolyml_la_SOURCES = \
    arb.cpp \
    basicio.cpp \
    bitmap.cpp \
    check_objects.cpp \
    diagnostics.cpp \
    errors.cpp \
    exporter.cpp \
    foreign.cpp \
    gc.cpp \
    gc_check_weak_ref.cpp \
    gc_copy_phase.cpp \
    gc_mark_phase.cpp \
    gc_share_phase.cpp \
    gc_update_phase.cpp \
    gctaskfarm.cpp \
    heapsizing.cpp \
    locking.cpp \
    memmgr.cpp \
    mpoly.cpp \
    network.cpp \
    objsize.cpp \
    osmem.cpp \
    pexport.cpp \
    poly_specific.cpp \
    polyffi.cpp \
    polystring.cpp \
    process_env.cpp \
    processes.cpp \
    profiling.cpp \
    quick_gc.cpp \
    realconv.cpp \
    reals.cpp \
    rts_module.cpp \
    run_time.cpp \
    save_vec.cpp \
    savestate.cpp \
    scanaddrs.cpp \
    sharedata.cpp \
    sighandler.cpp \
    statistics.cpp \
    timing.cpp \
    xwindows.cpp \
    $(ARCHSOURCE) $(EXPORTSOURCE) $(OSSOURCE)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = polyml.pc

# Convert the portable format to a native binary
polyexport.o: polyimport exportPoly.sml
	(cd ..; driver/polyimport driver/$(POLYIMPORT) < driver/exportPoly.sml > /dev/null)

# Special rule for x86 assembly code.
x86asmtemp.S: x86asm.asm
	sed -f $(srcdir)/masm2gas <  $(srcdir)/x86asm.asm > $@ || { rm -f $@; false; }

clean-local:
	rm -f x86asmtemp.S ppcasmtemp.s

# ToolchainBMIGuard.cmake -- invalidate C++23 module BMIs when the toolchain moves.
#
# @author Olumuyiwa Oluwasanmi
#
# THE PROBLEM, MEASURED RATHER THAN ANTICIPATED. A module BMI records the compiler
# that produced it and clang REFUSES one from a different compiler. CMake does not
# notice, because the compiler PATH does not change: /usr/local/bin/clang++ is
# upgraded IN PLACE -- 22 -> 23.1.0 -> 23.1.2 in this ecosystem's history, by
# overlay -- so the cache entry still looks valid and every stale `.pcm` survives.
#
# On 2026-10-05 ThinButQuickWebFramework's `build/` held 681 `.pcm` and 800
# `.modmap`, the oldest from 2026-04-10, under a cache pinned to Clang 22.1.0 while
# the toolchain was 23.1.2. A one-file edit could not be compiled there.
#
# THE COST IS THE DIAGNOSIS, NOT THE REBUILD, and that is why this is a build-system
# gate and not a note. Only the FIRST error names the real cause
# (`module file '...pcm' uses an older format`); every one after it reads as broken
# source -- `no namespace named 'http2'`, `no member named 'simd'`,
# `no type named 'FrameParser'` -- so the symptom points at symbols while the cause
# is a file nobody looked at. A sweep takes seconds; recognising why does not.
#
# WHICHEVER COPY RUNS FIRST PROTECTS THE WHOLE TREE, and it does NOT require the
# top-level project to be the one carrying the guard. The glob is recursive over
# CMAKE_BINARY_DIR -- the TOP-LEVEL binary directory even when this file is included
# from a submodule -- so sweeping once from anywhere covers sensen's BMIs, SGEE's,
# the logger's and tbqwf's together.
#
# THE FIRST VERSION OF THIS FILE RETURNED EARLY UNLESS IT WAS TOP-LEVEL, and that was
# wrong in the case that matters most: embedding one of these repositories in a parent
# that does NOT carry the guard left the parent with no protection at all, which is
# precisely the configuration a submodule is for. A GLOBAL property makes it
# once-per-configure instead of once-per-project, so inclusion is enough and the work
# is still never repeated.
#
# WHAT IT WILL NOT DO. Only generated module artefacts, and only inside the build
# directory: no source, no cache entry, nothing above it. A sweep that removed more
# than it had to would be worse than the staleness it fixes.
#
# THIS FILE IS COPIED BYTE-IDENTICALLY INTO EVERY TOP-LEVEL PROJECT and that identity
# is GATED (tools/check_bmi_guard_identity.sh). A copy in another repository has no
# mechanism of its own that could keep it honest -- the same reason this ecosystem
# gates its vendored protos by identity against the source of truth rather than by a
# recorded checksum somebody must remember to regenerate.
#
# Bump TOOLCHAIN_BMI_GUARD_VERSION when the contract changes, never for a comment.

set(TOOLCHAIN_BMI_GUARD_VERSION 2)

# ONCE PER CONFIGURE, from whichever copy got here first. A GLOBAL property, not a
# normal variable: `add_subdirectory` gives each project its own variable scope, so a
# plain guard variable would be invisible to siblings and every copy would sweep in
# turn. Sweeping twice is harmless and sweeping after a sibling has begun trusting a
# BMI is not, so "once" is the contract rather than an optimisation.
get_property(_tbg_already GLOBAL PROPERTY TOOLCHAIN_BMI_GUARD_RAN)
if(_tbg_already)
    return()
endif()
set_property(GLOBAL PROPERTY TOOLCHAIN_BMI_GUARD_RAN TRUE)

set(_tbg_stamp "${CMAKE_BINARY_DIR}/toolchain_bmi_guard.stamp")

# The identity includes the compiler's SIZE and MTIME as well as its version string.
# An in-place rebuild at the SAME version is still a different compiler, and this is
# an ecosystem that installs LLVM by overlay -- `include/c++/v1` has had to be
# replaced wholesale once already because an overlay adds and overwrites but never
# deletes.
if(EXISTS "${CMAKE_CXX_COMPILER}")
    file(SIZE "${CMAKE_CXX_COMPILER}" _tbg_size)
    file(TIMESTAMP "${CMAKE_CXX_COMPILER}" _tbg_mtime "%Y%m%d%H%M%S")
else()
    set(_tbg_size "unknown")
    set(_tbg_mtime "unknown")
endif()
set(_tbg_key
    "v${TOOLCHAIN_BMI_GUARD_VERSION}/${CMAKE_CXX_COMPILER_ID}/${CMAKE_CXX_COMPILER_VERSION}/${CMAKE_CXX_COMPILER}/${_tbg_size}/${_tbg_mtime}")

set(_tbg_was "")
if(EXISTS "${_tbg_stamp}")
    file(READ "${_tbg_stamp}" _tbg_was)
    string(STRIP "${_tbg_was}" _tbg_was)
endif()

function(_tbg_sweep reason)
    file(GLOB_RECURSE _tbg_victims "${CMAKE_BINARY_DIR}/*.pcm" "${CMAKE_BINARY_DIR}/*.modmap")
    list(LENGTH _tbg_victims _tbg_n)
    if(_tbg_n GREATER 0)
        file(REMOVE ${_tbg_victims})
        message(STATUS "ToolchainBMIGuard: ${reason} -- removed ${_tbg_n} stale .pcm/.modmap file(s)")
    else()
        message(STATUS "ToolchainBMIGuard: ${reason} -- no .pcm/.modmap present")
    endif()
endfunction()

if(_tbg_was STREQUAL "")
    # NO STAMP PLUS EXISTING BMIs IS THE CASE THIS EXISTS FOR, and the first version
    # of this guard got it wrong: it read "no stamp" as "fresh directory, nothing to
    # invalidate" and merely recorded -- which on the 1,481-file tree above would have
    # BLESSED every stale BMI. Provenance that is UNKNOWN is exactly as dangerous as
    # provenance that is known-stale.
    file(GLOB_RECURSE _tbg_orphans "${CMAKE_BINARY_DIR}/*.pcm" "${CMAKE_BINARY_DIR}/*.modmap")
    list(LENGTH _tbg_orphans _tbg_orphan_n)
    if(_tbg_orphan_n GREATER 0)
        _tbg_sweep("no stamp but ${_tbg_orphan_n} BMI(s) present, provenance unknown")
    endif()
    unset(_tbg_orphans)
    message(STATUS "ToolchainBMIGuard: recording ${_tbg_key}")
elseif(NOT _tbg_was STREQUAL "${_tbg_key}")
    message(STATUS "ToolchainBMIGuard: TOOLCHAIN CHANGED")
    message(STATUS "  was: ${_tbg_was}")
    message(STATUS "  now: ${_tbg_key}")
    _tbg_sweep("the toolchain moved")
else()
    message(STATUS "ToolchainBMIGuard: toolchain unchanged (${_tbg_key})")
endif()

file(WRITE "${_tbg_stamp}" "${_tbg_key}\n")

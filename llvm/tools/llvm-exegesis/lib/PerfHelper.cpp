//===-- PerfHelper.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PerfHelper.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#ifdef HAVE_LIBPFM
#include <perfmon/perf_event.h>
#include <perfmon/pfmlib.h>
#include <perfmon/pfmlib_perf_event.h>
#endif

#include <cassert>
#include <cstddef>
#include <errno.h>  // for erno
#include <string.h> // for strerror()

namespace llvm {
namespace exegesis {
namespace pfm {

#ifdef HAVE_LIBPFM
static bool isPfmError(int Code) { return Code != PFM_SUCCESS; }
#endif

bool pfmInitialize() {
#ifdef HAVE_LIBPFM
  return isPfmError(pfm_initialize());
#else
  return true;
#endif
}

void pfmTerminate() {
#ifdef HAVE_LIBPFM
  pfm_terminate();
#endif
}

// Performance counters may be unavailable for a number of reasons (such as
// kernel.perf_event_paranoid restriction or CPU being unknown to libpfm).
//
// Dummy event can be specified to skip interaction with real performance
// counters while still passing control to the generated code snippet.
const char *const PerfEvent::DummyEventString = "not-really-an-event";

PerfEvent::~PerfEvent() {
#ifdef HAVE_LIBPFM
  delete Attr;
  ;
#endif
}

PerfEvent::PerfEvent(PerfEvent &&Other)
    : EventString(std::move(Other.EventString)),
      FullQualifiedEventString(std::move(Other.FullQualifiedEventString)),
      Attr(Other.Attr) {
  Other.Attr = nullptr;
}

PerfEvent::PerfEvent(StringRef PfmEventString)
    : EventString(PfmEventString.str()), Attr(nullptr) {
  if (PfmEventString != DummyEventString)
    initRealEvent(PfmEventString);
  else
    FullQualifiedEventString = PfmEventString;
}

void PerfEvent::initRealEvent(StringRef PfmEventString) {
#ifdef HAVE_LIBPFM
  char *Fstr = nullptr;
  pfm_perf_encode_arg_t Arg = {};
  Attr = new perf_event_attr();
  Arg.attr = Attr;
  Arg.fstr = &Fstr;
  Arg.size = sizeof(pfm_perf_encode_arg_t);
  const int Result = pfm_get_os_event_encoding(EventString.c_str(), PFM_PLM3,
                                               PFM_OS_PERF_EVENT, &Arg);
  if (isPfmError(Result)) {
    // We don't know beforehand which counters are available (e.g. 6 uops ports
    // on Sandybridge but 8 on Haswell) so we report the missing counter without
    // crashing.
    errs() << pfm_strerror(Result) << " - cannot create event " << EventString
           << "\n";
  }
  if (Fstr) {
    FullQualifiedEventString = Fstr;
    free(Fstr);
  }
#endif
}

StringRef PerfEvent::name() const { return EventString; }

bool PerfEvent::valid() const { return !FullQualifiedEventString.empty(); }

const perf_event_attr *PerfEvent::attribute() const { return Attr; }

StringRef PerfEvent::getPfmEventString() const {
  return FullQualifiedEventString;
}

Counter::Counter(PerfEvent &&E, pid_t ProcessID) : Event(std::move(E)) {
  assert(Event.valid());
  IsDummyEvent = Event.name() == PerfEvent::DummyEventString;
  if (!IsDummyEvent)
    initRealEvent(E, ProcessID);
}

#ifdef HAVE_LIBPFM
void Counter::initRealEvent(const PerfEvent &E, pid_t ProcessID) {
  const int Cpu = -1;     // measure any processor.
  const int GroupFd = -1; // no grouping of counters.
  const uint32_t Flags = 0;
  perf_event_attr AttrCopy = *Event.attribute();
  FileDescriptor = perf_event_open(&AttrCopy, ProcessID, Cpu, GroupFd, Flags);
  if (FileDescriptor == -1) {
    errs() << "Unable to open event. ERRNO: " << strerror(errno)
           << ". Make sure your kernel allows user "
              "space perf monitoring.\nYou may want to try:\n$ sudo sh "
              "-c 'echo -1 > /proc/sys/kernel/perf_event_paranoid'.\n"
           << "If you are debugging and just want to execute the snippet "
              "without actually reading performance counters, "
              "pass --use-dummy-perf-counters command line option.\n";
  }
  assert(FileDescriptor != -1 && "Unable to open event");
}

Counter::~Counter() {
  if (!IsDummyEvent)
    close(FileDescriptor);
}

void Counter::start() {
  if (!IsDummyEvent)
    ioctl(FileDescriptor, PERF_EVENT_IOC_RESET, 0);
}

void Counter::stop() {
  if (!IsDummyEvent)
    ioctl(FileDescriptor, PERF_EVENT_IOC_DISABLE, 0);
}

llvm::Expected<llvm::SmallVector<int64_t, 4>>
Counter::readOrError(StringRef /*unused*/) const {
  int64_t Count = 0;
  if (!IsDummyEvent) {
    ssize_t ReadSize = ::read(FileDescriptor, &Count, sizeof(Count));
    if (ReadSize != sizeof(Count))
      return llvm::make_error<llvm::StringError>("Failed to read event counter",
                                                 llvm::errc::io_error);
  } else {
    Count = 42;
  }

  llvm::SmallVector<int64_t, 4> Result;
  Result.push_back(Count);
  return Result;
}

int Counter::numValues() const { return 1; }
#else

void Counter::initRealEvent(const PerfEvent &, pid_t ProcessID) {}

Counter::~Counter() = default;

void Counter::start() {}

void Counter::stop() {}

llvm::Expected<llvm::SmallVector<int64_t, 4>>
Counter::readOrError(StringRef /*unused*/) const {
  if (IsDummyEvent) {
    llvm::SmallVector<int64_t, 4> Result;
    Result.push_back(42);
    return Result;
  }
  return llvm::make_error<llvm::StringError>("Not implemented",
                                             llvm::errc::io_error);
}

int Counter::numValues() const { return 1; }

#endif

} // namespace pfm
} // namespace exegesis
} // namespace llvm

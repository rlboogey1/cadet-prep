#pragma once
// Safe saving and loading of the progress files (everything in apppaths::userFile()).
//
// write(): the whole file goes to "<path>.tmp" in the same folder, is flushed and
// closed, and only then renamed over the target. A crash, power loss or full disk
// leaves either the old file or the new one, never an empty or half-written one.
//
// setAside(): when a progress file exists but won't parse, call it from the loader's
// catch block, before anything can save over the file. It copies the damaged file
// to "<path>.corrupt-YYYYMMDD-HHMMSS" (never deletes it) and queues a notice.
//
// Both queue short notices ("Your progress file was damaged, so it was set aside as
// ...", "Couldn't save ...") that the GUI shows once and the console prints. Take them
// with takeNotices(), a few times a second is fine.
#include <string>
#include <vector>

namespace atomicfile {

// Writes `contents` to `path` atomically. Returns false (old file untouched, temp file
// removed, a "couldn't save" notice queued once until a save works again) on any failure.
bool write(const std::string& path, const std::string& contents);

// Copies the damaged file aside and queues a notice. Returns the new file's name
// (without the folder), or "" if the copy failed. If an identical copy was already
// set aside (the same damaged file loaded again before it was overwritten), that
// copy is reused instead of making another. The notice is queued once per run.
std::string setAside(const std::string& path);

// Notices queued since the last call, oldest first.
std::vector<std::string> takeNotices();

}  // namespace atomicfile

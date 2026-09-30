#pragma once

// How many handles of each kind this process holds: when handles grow, the
// kind that grows names the leak. For the soak and for tests that prove
// something leaks nothing.
//  - Windows: every kernel handle by type (Event, File, Key, Thread...), from
//    NtQueryInformationProcess(ProcessHandleInformation), as Process Explorer
//    does.
//  - Linux: every open file descriptor by kind (file, socket, pipe, sound
//    device, eventfd...), from /proc/self/fd, and the threads.
//  - macOS: open file descriptors from /dev/fd, and the threads (Mach).

#include <QString>

#include <map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <QDir>
#include <QFileInfo>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#endif

namespace gigchain::test {

#if defined(__APPLE__)
inline std::map<QString, int> handlesByType()
{
    std::map<QString, int> counts;
    const QDir fds(QStringLiteral("/dev/fd"));
    counts[QStringLiteral("file")] =
        static_cast<int>(fds.entryList(QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot).size()) - 1; // (the listing's own)
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS) {
        counts[QStringLiteral("Thread")] = static_cast<int>(count);
        for (mach_msg_type_number_t i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), threads[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): Mach's array
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Mach's array
    }
    return counts;
}
#elif !defined(_WIN32)
inline std::map<QString, int> handlesByType()
{
    std::map<QString, int> counts;
    const QDir fds(QStringLiteral("/proc/self/fd"));
    for (const QString& fd : fds.entryList(QDir::System | QDir::Files | QDir::NoDotAndDotDot)) {
        const QString target = QFileInfo(fds.filePath(fd)).symLinkTarget();
        QString kind = QStringLiteral("file");
        if (target.startsWith(QStringLiteral("socket:"))) kind = QStringLiteral("socket");
        else if (target.startsWith(QStringLiteral("pipe:"))) kind = QStringLiteral("pipe");
        else if (target.startsWith(QStringLiteral("anon_inode:"))) kind = target.section(u':', 1).remove(u'[').remove(u']');
        else if (target.startsWith(QStringLiteral("/dev/snd/"))) kind = QStringLiteral("sound device");
        else if (target.startsWith(QStringLiteral("/dev/"))) kind = QStringLiteral("device");
        ++counts[kind];
    }
    --counts[QStringLiteral("file")]; // the /proc/self/fd listing's own
    counts[QStringLiteral("Thread")] =
        static_cast<int>(QDir(QStringLiteral("/proc/self/task")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
    return counts;
}
#else
inline std::map<QString, int> handlesByType()
{
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-union-access, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays): Windows' raw structures
    struct Entry
    {
        HANDLE handle;
        ULONG_PTR handleCount;
        ULONG_PTR pointerCount;
        ULONG grantedAccess;
        ULONG typeIndex;
        ULONG attributes;
        ULONG reserved;
    };
    struct Snapshot
    {
        ULONG_PTR count;
        ULONG_PTR reserved;
        Entry entries[1];
    };
    struct TypeName
    {
        USHORT length;
        USHORT maximumLength;
        PWSTR buffer;
    };
    using Query = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto queryProcess = reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQueryInformationProcess"));
    auto queryObject = reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQueryObject"));
    std::map<QString, int> counts;
    if (queryProcess == nullptr || queryObject == nullptr) return counts;
    constexpr ULONG kProcessHandleInformation = 51;
    constexpr ULONG kObjectTypeInformation = 2;
    std::vector<unsigned char> buffer(std::size_t{1} << 20);
    ULONG needed = 0;
    if (queryProcess(GetCurrentProcess(), kProcessHandleInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &needed) < 0) {
        return counts;
    }
    const auto* snapshot = reinterpret_cast<const Snapshot*>(buffer.data());
    std::map<ULONG, QString> names;
    std::vector<unsigned char> typeBuffer(4096);
    for (ULONG_PTR i = 0; i < snapshot->count; ++i) {
        const Entry& entry = snapshot->entries[i];
        auto name = names.find(entry.typeIndex);
        if (name == names.end()) {
            QString typeName = QStringLiteral("type %1").arg(entry.typeIndex);
            if (queryObject(entry.handle, kObjectTypeInformation, typeBuffer.data(), static_cast<ULONG>(typeBuffer.size()), nullptr) >= 0) {
                const auto* type = reinterpret_cast<const TypeName*>(typeBuffer.data());
                typeName = QString::fromWCharArray(type->buffer, type->length / 2);
            }
            name = names.emplace(entry.typeIndex, typeName).first;
        }
        ++counts[name->second];
    }
    return counts;
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-union-access, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
}
#endif

inline int handlesOfType(const QString& type)
{
    const auto counts = handlesByType();
    const auto found = counts.find(type);
    return found == counts.end() ? 0 : found->second;
}

} // namespace gigchain::test

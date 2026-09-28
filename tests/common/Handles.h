#pragma once

// How many handles of each kind (Event, File, Key, Thread...) this process
// holds: when handles grow, the kind that grows names the leak. From
// NtQueryInformationProcess(ProcessHandleInformation), as Process Explorer
// does. For the soak and for tests that prove something leaks nothing.

#include <QString>

#include <map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gigchain::test {

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

inline int handlesOfType(const QString& type)
{
    const auto counts = handlesByType();
    const auto found = counts.find(type);
    return found == counts.end() ? 0 : found->second;
}

} // namespace gigchain::test

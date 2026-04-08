#include "TsEngine/Il2cpp.h"

#include <cstring>
#include <algorithm>

namespace TsEngine {

// ── 版本偏移预设 ──

Il2cppOffsets il2cppOffsetsForVersion(const std::string& ver) {
    Il2cppOffsets o;

    // 解析主版本号
    int major = 0;
    try { major = std::stoi(ver); } catch (...) {}

    if (major <= 2019) {
        // Unity 2018-2019 (il2cpp 24)
        o.klass_name         = 0x10;
        o.klass_namespace    = 0x18;
        o.klass_image        = 0x08;
        o.klass_parent       = 0x58;
        o.klass_fields       = 0x80;
        o.klass_methods      = 0x98;
        o.klass_field_count  = 0x118;
        o.klass_method_count = 0x11A;
        o.klass_instance_size = 0x104;
        o.klass_token        = 0x100;
        o.method_name        = 0x08;
        o.method_pointer     = 0x00;
        o.method_klass       = 0x10;
        o.method_return_type = 0x18;
        o.method_param_count = 0x26;
        o.method_token       = 0x2C;
    }
    else if (major == 2020) {
        // Unity 2020 (il2cpp 24.4-27)
        o.klass_name         = 0x10;
        o.klass_namespace    = 0x18;
        o.klass_image        = 0x08;
        o.klass_parent       = 0x58;
        o.klass_fields       = 0x80;
        o.klass_methods      = 0x98;
        o.klass_field_count  = 0x11C;
        o.klass_method_count = 0x11E;
        o.klass_instance_size = 0x108;
        o.klass_token        = 0x104;
        o.method_name        = 0x10;
        o.method_pointer     = 0x00;
        o.method_klass       = 0x18;
        o.method_return_type = 0x20;
        o.method_param_count = 0x2E;
        o.method_token       = 0x34;
    }
    else if (major == 2021 || major == 2022) {
        // Unity 2021-2022 (il2cpp 27-29) — 当前默认值
        // o 已经是默认构造, 不用改
    }
    else if (major >= 2023) {
        // Unity 2023+ / Unity 6 (il2cpp 31+)
        o.klass_name         = 0x10;
        o.klass_namespace    = 0x18;
        o.klass_image        = 0x08;
        o.klass_parent       = 0x58;
        o.klass_fields       = 0x90;
        o.klass_methods      = 0xA8;
        o.klass_field_count  = 0x130;
        o.klass_method_count = 0x132;
        o.klass_instance_size = 0x11C;
        o.klass_token        = 0x118;
        o.method_name        = 0x10;
        o.method_pointer     = 0x00;
        o.method_klass       = 0x18;
        o.method_return_type = 0x20;
        o.method_param_count = 0x2E;
        o.method_token       = 0x34;
    }

    return o;
}

// 从 libil2cpp.so 内存中搜索 Unity 版本字符串
// 格式: "20XX.Y.ZZf1" 在 .rodata 中
std::string detectUnityVersion(const Memory& mem, addr_t base, size_t size) {
    constexpr size_t BLOCK = 4096 * 16;
    std::vector<uint8_t> buf(BLOCK);

    for (size_t off = 0; off < size; off += BLOCK) {
        size_t chunk = std::min(BLOCK, size - off);
        if (!mem.readRaw(base + off, buf.data(), chunk)) continue;

        for (size_t i = 0; i + 12 < chunk; i++) {
            // 搜索 "20" 开头的版本字符串: "2019." / "2020." / "2021." / "2022." / "2023." / "6000."
            if (buf[i] == '2' && buf[i+1] == '0' && buf[i+2] >= '1' && buf[i+2] <= '2' &&
                buf[i+3] >= '0' && buf[i+3] <= '9' && buf[i+4] == '.') {
                // 验证后面是数字.数字
                size_t j = i + 5;
                while (j < chunk && (buf[j] >= '0' && buf[j] <= '9')) j++;
                if (j < chunk && buf[j] == '.') {
                    j++;
                    while (j < chunk && ((buf[j] >= '0' && buf[j] <= '9') || buf[j] == 'f' || buf[j] == 'a' || buf[j] == 'b' || buf[j] == 'p')) j++;
                    // 确认字符串终止
                    if (j < chunk && (buf[j] == '\0' || buf[j] == ' ' || buf[j] == '\n')) {
                        std::string ver(reinterpret_cast<char*>(&buf[i]), j - i);
                        // 额外验证: 不是随机数据
                        if (ver.size() >= 8 && ver.size() <= 30) {
                            return ver;
                        }
                    }
                }
            }
        }
    }
    return "";
}

bool Il2cppInspector::autoDetectVersion(addr_t il2cppBase, size_t il2cppSize) {
    detectedVersion_ = detectUnityVersion(mem_, il2cppBase, il2cppSize);
    if (detectedVersion_.empty()) return false;
    offsets_ = il2cppOffsetsForVersion(detectedVersion_);
    return true;
}

Il2cppInspector::Il2cppInspector(const Memory& mem, Il2cppOffsets offsets)
    : mem_(mem), offsets_(offsets) {}

std::string Il2cppInspector::typeName(uint8_t typeEnum) {
    switch (typeEnum) {
        case 0x00: return "END";
        case 0x01: return "void";
        case 0x02: return "bool";
        case 0x03: return "char";
        case 0x04: return "sbyte";
        case 0x05: return "byte";
        case 0x06: return "short";
        case 0x07: return "ushort";
        case 0x08: return "int";
        case 0x09: return "uint";
        case 0x0A: return "long";
        case 0x0B: return "ulong";
        case 0x0C: return "float";
        case 0x0D: return "double";
        case 0x0E: return "string";
        case 0x0F: return "Ptr";
        case 0x10: return "ByRef";
        case 0x11: return "ValueType";
        case 0x12: return "Class";
        case 0x13: return "Var";
        case 0x14: return "Array";
        case 0x15: return "GenericInst";
        case 0x16: return "TypedByRef";
        case 0x18: return "IntPtr";
        case 0x19: return "UIntPtr";
        case 0x1C: return "object";
        case 0x1D: return "SZArray";
        case 0x1E: return "MVar";
        default:   return "unknown(" + std::to_string(typeEnum) + ")";
    }
}

std::string Il2cppInspector::readTypeName(addr_t typeAddr) {
    if (typeAddr == 0) return "???";

    auto bits = mem_.read<uint64_t>(typeAddr + offsets_.type_bits);
    if (!bits) return "???";

    uint8_t typeEnum = static_cast<uint8_t>(*bits & 0x1F);

    // 对于 Class/ValueType，data 指向 Il2CppClass*，可以递归读类名
    if (typeEnum == 0x11 || typeEnum == 0x12) {
        auto klassPtr = mem_.read<addr_t>(typeAddr + offsets_.type_data);
        if (klassPtr && *klassPtr != 0) {
            auto namePtr = mem_.read<addr_t>(*klassPtr + offsets_.klass_name);
            if (namePtr && *namePtr != 0) {
                auto name = mem_.readString(*namePtr);
                if (name) return *name;
            }
        }
    }

    return typeName(typeEnum);
}

std::optional<Il2cppClassInfo> Il2cppInspector::inspectObject(addr_t instanceAddr) {
    // Il2CppObject 的第一个字段是 Il2CppClass* klass
    auto klassPtr = mem_.read<addr_t>(instanceAddr + offsets_.object_klass);
    if (!klassPtr || *klassPtr == 0) return std::nullopt;

    return readClass(*klassPtr);
}

std::optional<Il2cppClassInfo> Il2cppInspector::readClass(addr_t klassAddr) {
    Il2cppClassInfo info;
    info.klassAddr = klassAddr;

    // 读取类名
    auto namePtr = mem_.read<addr_t>(klassAddr + offsets_.klass_name);
    if (namePtr && *namePtr != 0) {
        auto s = mem_.readString(*namePtr);
        info.name = s.value_or("<unknown>");
    }

    // 命名空间
    auto nsPtr = mem_.read<addr_t>(klassAddr + offsets_.klass_namespace);
    if (nsPtr && *nsPtr != 0) {
        auto s = mem_.readString(*nsPtr);
        info.nameSpace = s.value_or("");
    }

    // 父类
    auto parentPtr = mem_.read<addr_t>(klassAddr + offsets_.klass_parent);
    if (parentPtr && *parentPtr != 0) {
        auto parentNamePtr = mem_.read<addr_t>(*parentPtr + offsets_.klass_name);
        if (parentNamePtr && *parentNamePtr != 0) {
            auto s = mem_.readString(*parentNamePtr);
            info.parentName = s.value_or("");
        }
    }

    // instance size & token
    auto instSize = mem_.read<uint32_t>(klassAddr + offsets_.klass_instance_size);
    info.instanceSize = instSize.value_or(0);

    auto token = mem_.read<uint32_t>(klassAddr + offsets_.klass_token);
    info.token = token.value_or(0);

    // 字段和方法
    info.fields  = readFields(klassAddr);
    info.methods = readMethods(klassAddr);

    return info;
}

std::vector<Il2cppFieldInfo> Il2cppInspector::readFields(addr_t klassAddr) {
    std::vector<Il2cppFieldInfo> result;

    auto fieldCount = mem_.read<uint16_t>(klassAddr + offsets_.klass_field_count);
    if (!fieldCount || *fieldCount == 0 || *fieldCount > 1000) return result;

    auto fieldsPtr = mem_.read<addr_t>(klassAddr + offsets_.klass_fields);
    if (!fieldsPtr || *fieldsPtr == 0) return result;

    addr_t base = *fieldsPtr;

    for (uint16_t i = 0; i < *fieldCount; i++) {
        addr_t fieldAddr = base + i * offsets_.field_struct_size;
        Il2cppFieldInfo fi;

        // 字段名
        auto namePtr = mem_.read<addr_t>(fieldAddr + offsets_.field_name);
        if (namePtr && *namePtr != 0) {
            auto s = mem_.readString(*namePtr);
            fi.name = s.value_or("<unknown>");
        }

        // 字段类型
        auto typePtr = mem_.read<addr_t>(fieldAddr + offsets_.field_type);
        fi.typeAddr = typePtr.value_or(0);
        fi.typeName = readTypeName(fi.typeAddr);

        // 字段偏移
        auto offset = mem_.read<int32_t>(fieldAddr + offsets_.field_offset);
        fi.offset = offset.value_or(-1);

        result.push_back(std::move(fi));
    }

    return result;
}

std::vector<Il2cppMethodInfo> Il2cppInspector::readMethods(addr_t klassAddr) {
    std::vector<Il2cppMethodInfo> result;

    auto methodCount = mem_.read<uint16_t>(klassAddr + offsets_.klass_method_count);
    if (!methodCount || *methodCount == 0 || *methodCount > 2000) return result;

    auto methodsPtr = mem_.read<addr_t>(klassAddr + offsets_.klass_methods);
    if (!methodsPtr || *methodsPtr == 0) return result;

    addr_t base = *methodsPtr;

    for (uint16_t i = 0; i < *methodCount; i++) {
        // methods 是 MethodInfo** (指针数组)
        auto methodPtr = mem_.read<addr_t>(base + i * offsets_.method_ptr_size);
        if (!methodPtr || *methodPtr == 0) continue;

        addr_t mAddr = *methodPtr;
        Il2cppMethodInfo mi;

        // 方法指针(代码地址)
        auto codePtr = mem_.read<addr_t>(mAddr + offsets_.method_pointer);
        mi.methodPointer = codePtr.value_or(0);

        // 方法名
        auto namePtr = mem_.read<addr_t>(mAddr + offsets_.method_name);
        if (namePtr && *namePtr != 0) {
            auto s = mem_.readString(*namePtr);
            mi.name = s.value_or("<unknown>");
        }

        // 返回类型
        auto retTypePtr = mem_.read<addr_t>(mAddr + offsets_.method_return_type);
        mi.returnTypeName = readTypeName(retTypePtr.value_or(0));

        // 参数数量
        auto pc = mem_.read<uint8_t>(mAddr + offsets_.method_param_count);
        mi.paramCount = pc.value_or(0);

        // token
        auto tok = mem_.read<uint32_t>(mAddr + offsets_.method_token);
        mi.token = tok.value_or(0);

        result.push_back(std::move(mi));
    }

    return result;
}

std::optional<FieldLookupResult> Il2cppInspector::findObjectByFieldAddr(addr_t fieldAddr) {
    // 从 fieldAddr 向前扫描, 步长 8 字节, 最多 4096 字节
    for (size_t off = 8; off <= 4096; off += 8) {
        addr_t candidate = fieldAddr - off;

        // 读候选 klass 指针
        auto klassPtr = mem_.read<addr_t>(candidate + offsets_.object_klass);
        if (!klassPtr || *klassPtr == 0) continue;
        addr_t klass = untag(*klassPtr);

        // 验证 klass: 能读到类名字符串?
        auto namePtr = mem_.read<addr_t>(klass + offsets_.klass_name);
        if (!namePtr || *namePtr == 0) continue;
        auto name = mem_.readString(*namePtr, 64);
        if (!name || name->empty()) continue;

        // 类名应是可打印 ASCII
        bool valid = true;
        for (char c : *name) {
            if (c < 0x20 || c > 0x7e) { valid = false; break; }
        }
        if (!valid) continue;

        // 验证 instanceSize
        auto instSize = mem_.read<uint32_t>(klass + offsets_.klass_instance_size);
        if (!instSize || *instSize == 0 || *instSize > 0x10000) continue;

        // fieldAddr 必须落在 [candidate, candidate + instanceSize) 内
        if (off > *instSize) continue;

        // 有效! 解析完整类信息
        auto classInfo = readClass(klass);
        if (!classInfo) continue;

        // 找匹配字段
        int32_t fieldOff = static_cast<int32_t>(off);
        FieldLookupResult result;
        result.instanceAddr = candidate;
        result.classInfo = std::move(*classInfo);
        result.matchedFieldOffset = fieldOff;

        for (const auto& f : result.classInfo.fields) {
            if (f.offset == fieldOff) {
                result.matchedFieldName = f.name;
                break;
            }
        }
        return result;
    }
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> Il2cppInspector::readFieldValue(
    addr_t instanceAddr, int32_t fieldOffset, size_t size) {
    return mem_.readBuffer(instanceAddr + fieldOffset, size);
}

} // namespace TsEngine

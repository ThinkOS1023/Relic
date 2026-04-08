#include "TsEngine/Il2cpp.h"

namespace TsEngine {

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

#pragma once

#include "Core.h"
#include "Memory.h"
#include <string>
#include <vector>

namespace TsEngine {

// Il2cpp 运行时结构体偏移量
// 不同 Unity 版本偏移不同, 用 il2cpp version 查看/设置
struct Il2cppOffsets {
    // Il2CppObject
    size_t object_klass  = 0x00;  // Il2CppClass* klass

    // Il2CppClass
    size_t klass_name         = 0x10;  // const char* name
    size_t klass_namespace    = 0x18;  // const char* namespaze
    size_t klass_parent       = 0x58;  // Il2CppClass* parent
    size_t klass_fields       = 0x80;  // FieldInfo* fields
    size_t klass_methods      = 0x98;  // MethodInfo** methods
    size_t klass_field_count  = 0x120; // uint16_t field_count
    size_t klass_method_count = 0x122; // uint16_t method_count
    size_t klass_instance_size = 0x10C; // uint32_t instance_size
    size_t klass_token        = 0x108; // uint32_t token
    size_t klass_image        = 0x08;  // Il2CppImage* image

    // FieldInfo (每个字段结构大小)
    size_t field_struct_size = 0x20;
    size_t field_name   = 0x00;  // const char* name
    size_t field_type   = 0x08;  // Il2CppType* type
    size_t field_offset = 0x18;  // int32_t offset

    // MethodInfo
    size_t method_ptr_size     = 0x08;  // methods 是指针数组, 每项8字节
    size_t method_pointer      = 0x00;  // void* methodPointer
    size_t method_name         = 0x10;  // const char* name
    size_t method_klass        = 0x18;  // Il2CppClass* klass
    size_t method_return_type  = 0x20;  // Il2CppType* return_type
    size_t method_param_count  = 0x2E;  // uint8_t parameters_count
    size_t method_token        = 0x34;  // uint32_t token

    // Il2CppType
    size_t type_data = 0x00;
    size_t type_bits = 0x08; // 低5位是 type enum
};

// 获取特定 Unity 版本的偏移预设
// 支持: "2019", "2020", "2021", "2022", "2023"
Il2cppOffsets il2cppOffsetsForVersion(const std::string& version);

// 从 libil2cpp.so 内存中检测 Unity 版本字符串
std::string detectUnityVersion(const Memory& mem, addr_t il2cppBase, size_t il2cppSize);

struct Il2cppFieldInfo {
    std::string name;
    std::string typeName;
    int32_t offset;
    addr_t typeAddr;
};

struct Il2cppMethodInfo {
    std::string name;
    addr_t methodPointer;
    std::string returnTypeName;
    uint8_t paramCount;
    uint32_t token;
};

struct Il2cppClassInfo {
    std::string name;
    std::string nameSpace;
    std::string parentName;
    uint32_t instanceSize;
    uint32_t token;
    addr_t klassAddr;
    std::vector<Il2cppFieldInfo> fields;
    std::vector<Il2cppMethodInfo> methods;
};

// 反向查找结果: 从字段数据地址反推到所属对象
struct FieldLookupResult {
    addr_t instanceAddr;            // 对象实例地址
    Il2cppClassInfo classInfo;      // 完整类信息
    std::string matchedFieldName;   // 匹配的字段名 (空=无精确匹配)
    int32_t matchedFieldOffset;     // 匹配的字段偏移
};

class Il2cppInspector {
public:
    Il2cppInspector(const Memory& mem, Il2cppOffsets offsets = {});

    // 从实例地址解析对象信息
    std::optional<Il2cppClassInfo> inspectObject(addr_t instanceAddr);

    // 从字段数据地址反推对象实例 + 类信息
    // 向前扫描找 Il2CppClass* klass 指针, 验证后解析完整类信息
    std::optional<FieldLookupResult> findObjectByFieldAddr(addr_t fieldAddr);

    // 从 Il2CppClass* 地址解析类信息
    std::optional<Il2cppClassInfo> readClass(addr_t klassAddr);

    // 单独读取字段/方法
    std::vector<Il2cppFieldInfo> readFields(addr_t klassAddr);
    std::vector<Il2cppMethodInfo> readMethods(addr_t klassAddr);

    // 读取实例中某个字段的值
    std::optional<std::vector<uint8_t>> readFieldValue(addr_t instanceAddr, int32_t fieldOffset, size_t size);

    // il2cpp 类型枚举值 → 类型名
    static std::string typeName(uint8_t typeEnum);

    const Il2cppOffsets& offsets() const { return offsets_; }
    void setOffsets(const Il2cppOffsets& o) { offsets_ = o; }

    // 自动检测版本并设置偏移
    bool autoDetectVersion(addr_t il2cppBase, size_t il2cppSize);
    const std::string& detectedVersion() const { return detectedVersion_; }

private:
    std::string readTypeName(addr_t typeAddr);

    const Memory& mem_;
    Il2cppOffsets offsets_;
    std::string detectedVersion_;
};

} // namespace TsEngine

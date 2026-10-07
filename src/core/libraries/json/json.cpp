// SPDX-FileCopyrightText: Copyright 2026 nhlport contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/json/json.h"
#include "core/libraries/libs.h"

namespace Libraries::Json {

// sce::Json HLE. The game constructs sce::Json::Value / Initializer / MemAllocator objects in its
// own memory but only ever touches them through the methods imported from libSceJson2, so the
// real state lives here, keyed by the address of the game's object. Values returned by
// operator[] and getString() are host objects; the game passes them straight back into these
// functions and never dereferences them itself.

enum class ValueType : u32 {
    Null = 0,
    Boolean = 1,
    Integer = 2,
    UInteger = 3,
    Real = 4,
    String = 5,
    Array = 6,
    Object = 7,
};

// Error codes of libSceJson2 are not documented publicly; a generic parse error is reported.
constexpr s32 ORBIS_JSON_ERROR_PARSE = 0x80430001;

struct StringNode {
    std::string text;
};

struct Node {
    ValueType type{ValueType::Null};
    bool boolean{};
    s64 integer{};
    u64 uinteger{};
    double real{};
    StringNode string{};
    std::vector<std::unique_ptr<Node>> array{};
    std::vector<std::pair<std::string, std::unique_ptr<Node>>> object{};
};

static std::mutex g_mutex;
// Game-owned Value storage -> node.
static std::unordered_map<const void*, std::unique_ptr<Node>> g_roots;
// Every node reachable from a root, so pointers coming back from the game can be validated.
static std::unordered_set<const Node*> g_live;
static std::unordered_set<const StringNode*> g_live_strings;
static Node g_null;
static StringNode g_empty_string;

static void Track(Node& n) {
    g_live.insert(&n);
    g_live_strings.insert(&n.string);
    for (auto& e : n.array) {
        Track(*e);
    }
    for (auto& [k, v] : n.object) {
        Track(*v);
    }
}

static void Untrack(Node& n) {
    g_live.erase(&n);
    g_live_strings.erase(&n.string);
    for (auto& e : n.array) {
        Untrack(*e);
    }
    for (auto& [k, v] : n.object) {
        Untrack(*v);
    }
}

static void Reset(Node& n) {
    Untrack(n);
    n = Node{};
    Track(n);
}

static void Build(Node& n, const nlohmann::json& j) {
    using value_t = nlohmann::json::value_t;
    switch (j.type()) {
    case value_t::boolean:
        n.type = ValueType::Boolean;
        n.boolean = j.get<bool>();
        break;
    case value_t::number_integer:
        n.type = ValueType::Integer;
        n.integer = j.get<s64>();
        n.uinteger = static_cast<u64>(n.integer);
        n.real = static_cast<double>(n.integer);
        break;
    case value_t::number_unsigned:
        n.type = ValueType::UInteger;
        n.uinteger = j.get<u64>();
        n.integer = static_cast<s64>(n.uinteger);
        n.real = static_cast<double>(n.uinteger);
        break;
    case value_t::number_float:
        n.type = ValueType::Real;
        n.real = j.get<double>();
        n.integer = static_cast<s64>(n.real);
        n.uinteger = static_cast<u64>(n.real);
        break;
    case value_t::string:
        n.type = ValueType::String;
        n.string.text = j.get<std::string>();
        break;
    case value_t::array:
        n.type = ValueType::Array;
        for (const auto& e : j) {
            auto child = std::make_unique<Node>();
            Build(*child, e);
            n.array.push_back(std::move(child));
        }
        break;
    case value_t::object:
        n.type = ValueType::Object;
        for (const auto& [key, value] : j.items()) {
            auto child = std::make_unique<Node>();
            Build(*child, value);
            n.object.emplace_back(key, std::move(child));
        }
        break;
    default:
        n.type = ValueType::Null;
        break;
    }
}

// Resolves the `this` pointer of a Value method: a game-owned root or one of our nodes.
static Node* Lookup(const void* self) {
    if (const auto it = g_roots.find(self); it != g_roots.end()) {
        return it->second.get();
    }
    const auto* node = static_cast<const Node*>(self);
    if (g_live.contains(node)) {
        return const_cast<Node*>(node);
    }
    LOG_WARNING(Lib_Json, "Value method called on unknown object {}", self);
    return &g_null;
}

static Node& Root(const void* self) {
    auto& slot = g_roots[self];
    if (!slot) {
        slot = std::make_unique<Node>();
        Track(*slot);
    }
    return *slot;
}

// ---- sce::Json::Initializer

void PS4_SYSV_ABI Initializer_Ctor(void* self) {
    LOG_DEBUG(Lib_Json, "Initializer::Initializer {}", self);
}

void PS4_SYSV_ABI Initializer_Dtor(void* self) {
    LOG_DEBUG(Lib_Json, "Initializer::~Initializer {}", self);
}

s32 PS4_SYSV_ABI Initializer_initialize(void* self, const void* init_parameter) {
    LOG_INFO(Lib_Json, "Initializer::initialize {} param {}", self, init_parameter);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI Initializer_terminate(void* self) {
    LOG_INFO(Lib_Json, "Initializer::terminate {}", self);
    return ORBIS_OK;
}

// ---- sce::Json::MemAllocator (abstract base; the game's subclass provides allocate/deallocate)

void PS4_SYSV_ABI MemAllocator_Ctor(void* self) {
    LOG_DEBUG(Lib_Json, "MemAllocator::MemAllocator {}", self);
}

void PS4_SYSV_ABI MemAllocator_Dtor(void* self) {
    LOG_DEBUG(Lib_Json, "MemAllocator::~MemAllocator {}", self);
}

void PS4_SYSV_ABI MemAllocator_notifyError(void* self, s32 error, u64 size, void* context) {
    LOG_WARNING(Lib_Json, "MemAllocator::notifyError error {:#x} size {} context {}", error, size,
                context);
}

// ---- sce::Json::Value

void PS4_SYSV_ABI Value_Ctor(void* self) {
    std::scoped_lock lk{g_mutex};
    auto node = std::make_unique<Node>();
    Track(*node);
    if (const auto it = g_roots.find(self); it != g_roots.end()) {
        Untrack(*it->second);
    }
    g_roots[self] = std::move(node);
    LOG_TRACE(Lib_Json, "Value::Value {}", self);
}

void PS4_SYSV_ABI Value_Dtor(void* self) {
    std::scoped_lock lk{g_mutex};
    if (const auto it = g_roots.find(self); it != g_roots.end()) {
        Untrack(*it->second);
        g_roots.erase(it);
    }
    LOG_TRACE(Lib_Json, "Value::~Value {}", self);
}

void PS4_SYSV_ABI Value_clear(void* self) {
    std::scoped_lock lk{g_mutex};
    Node* n = Lookup(self);
    if (n != &g_null) {
        Reset(*n);
    }
}

void PS4_SYSV_ABI Value_setNullAccessCallBack(void* self, void* callback, void* context) {
    // The callback would let the game substitute a value for missing members; a null value is
    // returned instead, which is also the library's default behaviour.
    LOG_DEBUG(Lib_Json, "Value::setNullAccessCallBack {} cb {} ctx {}", self, callback, context);
}

bool PS4_SYSV_ABI Value_getBoolean(const void* self) {
    std::scoped_lock lk{g_mutex};
    return Lookup(self)->boolean;
}

s64 PS4_SYSV_ABI Value_getInteger(const void* self) {
    std::scoped_lock lk{g_mutex};
    return Lookup(self)->integer;
}

u64 PS4_SYSV_ABI Value_getUInteger(const void* self) {
    std::scoped_lock lk{g_mutex};
    return Lookup(self)->uinteger;
}

u64 PS4_SYSV_ABI Value_count(const void* self) {
    std::scoped_lock lk{g_mutex};
    const Node* n = Lookup(self);
    switch (n->type) {
    case ValueType::Array:
        return n->array.size();
    case ValueType::Object:
        return n->object.size();
    default:
        return 0;
    }
}

ValueType PS4_SYSV_ABI Value_getType(const void* self) {
    std::scoped_lock lk{g_mutex};
    return Lookup(self)->type;
}

const StringNode* PS4_SYSV_ABI Value_getString(const void* self) {
    std::scoped_lock lk{g_mutex};
    const Node* n = Lookup(self);
    return n->type == ValueType::String ? &n->string : &g_empty_string;
}

const Node* PS4_SYSV_ABI Value_indexKey(const void* self, const char* key) {
    std::scoped_lock lk{g_mutex};
    const Node* n = Lookup(self);
    if (key && n->type == ValueType::Object) {
        for (const auto& [k, v] : n->object) {
            if (k == key) {
                return v.get();
            }
        }
    }
    LOG_DEBUG(Lib_Json, "Value[\"{}\"] not found (type {})", key ? key : "(null)",
              static_cast<u32>(n->type));
    return &g_null;
}

const Node* PS4_SYSV_ABI Value_indexPos(const void* self, u64 index) {
    std::scoped_lock lk{g_mutex};
    const Node* n = Lookup(self);
    if (n->type == ValueType::Array && index < n->array.size()) {
        return n->array[index].get();
    }
    if (n->type == ValueType::Object && index < n->object.size()) {
        return n->object[index].second.get();
    }
    LOG_DEBUG(Lib_Json, "Value[{}] out of range (type {})", index, static_cast<u32>(n->type));
    return &g_null;
}

// ---- sce::Json::String

const char* PS4_SYSV_ABI String_c_str(const void* self) {
    std::scoped_lock lk{g_mutex};
    const auto* s = static_cast<const StringNode*>(self);
    if (s == &g_empty_string || g_live_strings.contains(s)) {
        return s->text.c_str();
    }
    LOG_WARNING(Lib_Json, "String::c_str called on unknown object {}", self);
    return "";
}

// ---- sce::Json::Parser

s32 PS4_SYSV_ABI Parser_parse(void* root, const char* buffer, u64 size) {
    if (!buffer) {
        // NHL 24 does this when a config file it tried to read is missing; the real library
        // rejects the call as well and the game carries on.
        LOG_WARNING(Lib_Json, "Parser::parse with null buffer (size {})", size);
        return ORBIS_JSON_ERROR_PARSE;
    }
    const std::string_view text{buffer, size != 0 ? size : std::strlen(buffer)};
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false, true);
    std::scoped_lock lk{g_mutex};
    Node& n = Root(root);
    if (j.is_discarded()) {
        LOG_ERROR(Lib_Json, "Parser::parse failed ({} bytes): {:.120}", text.size(), text);
        Reset(n);
        return ORBIS_JSON_ERROR_PARSE;
    }
    Reset(n);
    Build(n, j);
    Track(n);
    LOG_INFO(Lib_Json, "Parser::parse ok: {} bytes, root type {}, {} entries", text.size(),
             static_cast<u32>(n.type), n.type == ValueType::Array ? n.array.size() : n.object.size());
    return ORBIS_OK;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("cK6bYHf-Q5E", "libSceJson2", 1, "libSceJson", Initializer_Ctor);
    LIB_FUNCTION("RujUxbr3haM", "libSceJson2", 1, "libSceJson", Initializer_Dtor);
    LIB_FUNCTION("Cxwy7wHq4J0", "libSceJson2", 1, "libSceJson", Initializer_initialize);
    LIB_FUNCTION("PR5k1penBLM", "libSceJson2", 1, "libSceJson", Initializer_terminate);
    LIB_FUNCTION("-hJRce8wn1U", "libSceJson2", 1, "libSceJson", MemAllocator_Ctor);
    LIB_FUNCTION("OcAgPxcq5Vk", "libSceJson2", 1, "libSceJson", MemAllocator_Dtor);
    LIB_FUNCTION("i-XwZjw0OOY", "libSceJson2", 1, "libSceJson", MemAllocator_notifyError);
    LIB_FUNCTION("qBMjqyBn3OM", "libSceJson2", 1, "libSceJson", Value_Ctor);
    LIB_FUNCTION("WTtYf+cNnXI", "libSceJson2", 1, "libSceJson", Value_Dtor);
    LIB_FUNCTION("FIjXN2TkuTs", "libSceJson2", 1, "libSceJson", Value_clear);
    LIB_FUNCTION("Xbl-LYVFNEE", "libSceJson2", 1, "libSceJson", Value_setNullAccessCallBack);
    LIB_FUNCTION("zTwZdI8AZ5Y", "libSceJson2", 1, "libSceJson", Value_getBoolean);
    LIB_FUNCTION("DIxvoy7Ngvk", "libSceJson2", 1, "libSceJson", Value_getInteger);
    LIB_FUNCTION("sn4HNCtNRzY", "libSceJson2", 1, "libSceJson", Value_getUInteger);
    LIB_FUNCTION("RBw+4NukeGQ", "libSceJson2", 1, "libSceJson", Value_count);
    LIB_FUNCTION("SHtAad20YYM", "libSceJson2", 1, "libSceJson", Value_getType);
    LIB_FUNCTION("epJ6x2LV0kU", "libSceJson2", 1, "libSceJson", Value_getString);
    LIB_FUNCTION("HwDt5lD9Bfo", "libSceJson2", 1, "libSceJson", Value_indexKey);
    LIB_FUNCTION("XlWbvieLj2M", "libSceJson2", 1, "libSceJson", Value_indexPos);
    LIB_FUNCTION("L1KAkYWml-M", "libSceJson2", 1, "libSceJson", String_c_str);
    LIB_FUNCTION("S5JxQnoGF3E", "libSceJson2", 1, "libSceJson", Parser_parse);
}

} // namespace Libraries::Json

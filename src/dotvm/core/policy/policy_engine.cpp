/// @file policy_engine.cpp
/// @brief SEC-009 Policy engine implementation

#include "dotvm/core/policy/policy_engine.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "dotvm/core/opcode.hpp"

namespace dotvm::core::policy {

namespace {

/// Opcode name to value mapping
const std::unordered_map<std::string_view, std::uint8_t> OPCODE_MAP = {
    // Arithmetic
    {"ADD", opcode::ADD},
    {"SUB", opcode::SUB},
    {"MUL", opcode::MUL},
    {"DIV", opcode::DIV},
    {"MOD", opcode::MOD},
    {"NEG", opcode::NEG},
    {"ADDI", opcode::ADDI},
    {"SUBI", opcode::SUBI},
    {"MULI", opcode::MULI},

    // Floating point
    {"FADD", opcode::FADD},
    {"FSUB", opcode::FSUB},
    {"FMUL", opcode::FMUL},
    {"FDIV", opcode::FDIV},
    {"FNEG", opcode::FNEG},
    {"FSQRT", opcode::FSQRT},
    {"FCMP", opcode::FCMP},
    {"F2I", opcode::F2I},
    {"I2F", opcode::I2F},

    // Bitwise
    {"AND", opcode::AND},
    {"OR", opcode::OR},
    {"XOR", opcode::XOR},
    {"NOT", opcode::NOT},
    {"SHL", opcode::SHL},
    {"SHR", opcode::SHR},
    {"SAR", opcode::SAR},
    {"ROL", opcode::ROL},
    {"ROR", opcode::ROR},
    {"SHLI", opcode::SHLI},
    {"SHRI", opcode::SHRI},
    {"SARI", opcode::SARI},
    {"ANDI", opcode::ANDI},
    {"ORI", opcode::ORI},
    {"XORI", opcode::XORI},

    // Comparison
    {"EQ", opcode::EQ},
    {"NE", opcode::NE},
    {"LT", opcode::LT},
    {"LE", opcode::LE},
    {"GT", opcode::GT},
    {"GE", opcode::GE},
    {"LTU", opcode::LTU},
    {"LEU", opcode::LEU},
    {"GTU", opcode::GTU},
    {"GEU", opcode::GEU},
    {"TEST", opcode::TEST},
    {"CMPI_EQ", opcode::CMPI_EQ},
    {"CMPI_NE", opcode::CMPI_NE},
    {"CMPI_LT", opcode::CMPI_LT},
    {"CMPI_GE", opcode::CMPI_GE},

    // Control flow
    {"JMP", opcode::JMP},
    {"JZ", opcode::JZ},
    {"JNZ", opcode::JNZ},
    {"BEQ", opcode::BEQ},
    {"BNE", opcode::BNE},
    {"BLT", opcode::BLT},
    {"BLE", opcode::BLE},
    {"BGT", opcode::BGT},
    {"BGE", opcode::BGE},
    {"CALL", opcode::CALL},
    {"RET", opcode::RET},
    {"TRY", opcode::TRY},
    {"CATCH", opcode::CATCH},
    {"THROW", opcode::THROW},
    {"ENDTRY", opcode::ENDTRY},
    {"HALT", opcode::HALT},

    // Memory
    {"LOAD8", opcode::LOAD8},
    {"LOAD16", opcode::LOAD16},
    {"LOAD32", opcode::LOAD32},
    {"LOAD64", opcode::LOAD64},
    {"STORE8", opcode::STORE8},
    {"STORE16", opcode::STORE16},
    {"STORE32", opcode::STORE32},
    {"STORE64", opcode::STORE64},
    {"LEA", opcode::LEA},

    // Crypto
    {"HASH_SHA256", opcode::HASH_SHA256},
    {"HASH_BLAKE3", opcode::HASH_BLAKE3},
    {"HASH_KECCAK", opcode::HASH_KECCAK},
    {"SIGN_ED25519", opcode::SIGN_ED25519},
    {"VERIFY_ED25519", opcode::VERIFY_ED25519},
    {"ENCRYPT_AES256", opcode::ENCRYPT_AES256},
    {"DECRYPT_AES256", opcode::DECRYPT_AES256},

    // System
    {"NOP", opcode::NOP},
    {"BREAK", opcode::BREAK},
    {"DEBUG", opcode::DEBUG},
    {"SYSCALL", opcode::SYSCALL},
};

/// Action name to Decision mapping
const std::unordered_map<std::string_view, Decision> ACTION_MAP = {
    {"Allow", Decision::Allow},
    {"Deny", Decision::Deny},
    {"RequireCapability", Decision::RequireCapability},
    {"Audit", Decision::Audit},
};

}  // namespace

// ============================================================================
// Policy Loading
// ============================================================================

Result<void, PolicyErrorInfo> PolicyEngine::load_policy(std::string_view json_string) {
    std::lock_guard<std::mutex> lock{write_mutex_};

    // Parse JSON
    auto parse_result = JsonParser::parse(json_string);
    if (!parse_result) {
        return parse_result.error();
    }

    const JsonValue& root = parse_result.value();

    // Parse policy structure
    auto policy_result = parse_policy(root);
    if (!policy_result) {
        return policy_result.error();
    }

    Policy policy = std::move(policy_result).value();

    // Compile decision tree
    auto new_state = std::make_shared<PolicyState>();
    new_state->tree.compile(policy.rules, policy.default_action);
    new_state->name = policy.name;
    new_state->version = policy.version;

    // Atomic swap
    active_state_.store(new_state, std::memory_order_release);

    // Store source for reload
    source_json_ = std::string{json_string};
    source_path_.reset();

    return Result<void, PolicyErrorInfo>{};
}

Result<void, PolicyErrorInfo> PolicyEngine::load_policy_file(std::string_view path) {
    std::lock_guard<std::mutex> lock{write_mutex_};

    // Read file
    std::ifstream file{std::string{path}};
    if (!file) {
        return PolicyErrorInfo::err(PolicyError::FileNotFound,
                                    std::string{"Cannot open file: "} + std::string{path});
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (!file) {
        return PolicyErrorInfo::err(PolicyError::FileReadError,
                                    std::string{"Error reading file: "} + std::string{path});
    }

    std::string content = buffer.str();

    // Parse JSON
    auto parse_result = JsonParser::parse(content);
    if (!parse_result) {
        return parse_result.error();
    }

    const JsonValue& root = parse_result.value();

    // Parse policy structure
    auto policy_result = parse_policy(root);
    if (!policy_result) {
        return policy_result.error();
    }

    Policy policy = std::move(policy_result).value();

    // Compile decision tree
    auto new_state = std::make_shared<PolicyState>();
    new_state->tree.compile(policy.rules, policy.default_action);
    new_state->name = policy.name;
    new_state->version = policy.version;

    // Atomic swap
    active_state_.store(new_state, std::memory_order_release);

    // Store source for reload
    source_path_ = std::string{path};
    source_json_.reset();

    return Result<void, PolicyErrorInfo>{};
}

Result<void, PolicyErrorInfo> PolicyEngine::reload_policy() {
    // Note: write_mutex_ will be acquired in load_policy/load_policy_file

    if (source_path_.has_value()) {
        return load_policy_file(*source_path_);
    }

    if (source_json_.has_value()) {
        return load_policy(*source_json_);
    }

    return PolicyErrorInfo::err(PolicyError::NoPolicyLoaded, "No policy source to reload");
}

// ============================================================================
// Policy Evaluation
// ============================================================================

PolicyDecision PolicyEngine::evaluate(const EvaluationContext& ctx, std::uint8_t opcode,
                                      std::string_view state_key) const {
    auto state = active_state_.load(std::memory_order_acquire);

    if (!state) {
        // No policy loaded - default allow
        return PolicyDecision::allow();
    }

    return state->tree.evaluate(ctx, opcode, state_key);
}

// ============================================================================
// Status Queries
// ============================================================================

bool PolicyEngine::has_policy() const noexcept {
    return active_state_.load(std::memory_order_acquire) != nullptr;
}

std::size_t PolicyEngine::rule_count() const noexcept {
    auto state = active_state_.load(std::memory_order_acquire);
    if (!state) return 0;
    return state->tree.rule_count();
}

std::optional<std::string> PolicyEngine::policy_name() const noexcept {
    auto state = active_state_.load(std::memory_order_acquire);
    if (!state || state->name.empty()) return std::nullopt;
    return state->name;
}

std::optional<std::string> PolicyEngine::policy_version() const noexcept {
    auto state = active_state_.load(std::memory_order_acquire);
    if (!state || state->version.empty()) return std::nullopt;
    return state->version;
}

// ============================================================================
// Parsing Helpers
// ============================================================================

Result<Policy, PolicyErrorInfo> PolicyEngine::parse_policy(const JsonValue& root) {
    if (!root.is_object()) {
        return PolicyErrorInfo::err(PolicyError::InvalidFieldType, "Policy must be an object");
    }

    Policy policy;

    // Parse rules array
    if (root.contains("rules")) {
        const JsonValue& rules_json = root["rules"];
        if (!rules_json.is_array()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType, "'rules' must be an array");
        }

        for (std::size_t i = 0; i < rules_json.size(); ++i) {
            auto rule_result = parse_rule(rules_json[i]);
            if (!rule_result) {
                return rule_result.error();
            }
            policy.rules.push_back(std::move(rule_result).value());
        }
    }

    // Parse default_action
    if (root.contains("default_action")) {
        const JsonValue& default_json = root["default_action"];
        if (!default_json.is_string()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'default_action' must be a string");
        }

        auto it = ACTION_MAP.find(default_json.as_string());
        if (it == ACTION_MAP.end()) {
            return PolicyErrorInfo::err(PolicyError::InvalidAction,
                                        "Unknown default_action: " + default_json.as_string());
        }
        policy.default_action = it->second;
    }

    // Parse optional metadata
    if (root.contains("name") && root["name"].is_string()) {
        policy.name = root["name"].as_string();
    }

    if (root.contains("version") && root["version"].is_string()) {
        policy.version = root["version"].as_string();
    }

    return policy;
}

Result<Rule, PolicyErrorInfo> PolicyEngine::parse_rule(const JsonValue& rule_json) {
    if (!rule_json.is_object()) {
        return PolicyErrorInfo::err(PolicyError::InvalidFieldType, "Rule must be an object");
    }

    Rule rule;

    // Parse id (required)
    if (!rule_json.contains("id")) {
        return PolicyErrorInfo::err(PolicyError::MissingField, "Rule missing 'id' field");
    }
    if (!rule_json["id"].is_int()) {
        return PolicyErrorInfo::err(PolicyError::InvalidFieldType, "'id' must be an integer");
    }
    std::int64_t id = rule_json["id"].as_int();
    if (id <= 0) {
        return PolicyErrorInfo::err(PolicyError::InvalidRuleId, "Rule 'id' must be positive");
    }
    rule.id = static_cast<std::uint64_t>(id);

    // Parse priority (optional, defaults to 0)
    if (rule_json.contains("priority")) {
        if (!rule_json["priority"].is_int()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'priority' must be an integer");
        }
        rule.priority = static_cast<std::int32_t>(rule_json["priority"].as_int());
    }

    // Parse conditions ("if" block)
    if (rule_json.contains("if")) {
        auto cond_result = parse_conditions(rule_json["if"]);
        if (!cond_result) {
            return cond_result.error();
        }
        rule.conditions = std::move(cond_result).value();
    }

    // Parse action ("then" block)
    if (!rule_json.contains("then")) {
        return PolicyErrorInfo::err(PolicyError::MissingField, "Rule missing 'then' field");
    }
    auto action_result = parse_action(rule_json["then"]);
    if (!action_result) {
        return action_result.error();
    }
    rule.action = std::move(action_result).value();

    return rule;
}

Result<std::vector<Condition>, PolicyErrorInfo> PolicyEngine::parse_conditions(
    const JsonValue& if_json) {
    if (!if_json.is_object()) {
        return PolicyErrorInfo::err(PolicyError::InvalidFieldType, "'if' must be an object");
    }

    std::vector<Condition> conditions;

    // Parse opcode
    if (if_json.contains("opcode")) {
        const JsonValue& opcode_json = if_json["opcode"];
        if (!opcode_json.is_string()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType, "'opcode' must be a string");
        }

        auto opcode_value = parse_opcode_name(opcode_json.as_string());
        if (!opcode_value) {
            return PolicyErrorInfo::err(PolicyError::UnknownOpcode,
                                        "Unknown opcode: " + opcode_json.as_string());
        }

        OpcodeCondition cond;
        cond.opcode = *opcode_value;
        cond.opcode_name = opcode_json.as_string();
        conditions.push_back(cond);
    }

    // Parse key_prefix
    if (if_json.contains("key_prefix")) {
        const JsonValue& prefix_json = if_json["key_prefix"];
        if (!prefix_json.is_string()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'key_prefix' must be a string");
        }

        KeyPrefixCondition cond;
        cond.prefix = prefix_json.as_string();
        conditions.push_back(cond);
    }

    // Parse dot_id
    if (if_json.contains("dot_id")) {
        const JsonValue& dot_json = if_json["dot_id"];
        if (!dot_json.is_int()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'dot_id' must be an integer");
        }

        DotIdCondition cond;
        cond.dot_id = static_cast<std::uint64_t>(dot_json.as_int());
        conditions.push_back(cond);
    }

    // Parse capability_has
    if (if_json.contains("capability_has")) {
        const JsonValue& cap_json = if_json["capability_has"];
        if (!cap_json.is_string()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'capability_has' must be a string");
        }

        CapabilityCondition cond;
        cond.capability_name = cap_json.as_string();
        conditions.push_back(cond);
    }

    // Parse memory_region
    if (if_json.contains("memory_region")) {
        const JsonValue& region_json = if_json["memory_region"];
        if (!region_json.is_object()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'memory_region' must be an object");
        }

        MemoryRegionCondition cond;

        if (region_json.contains("start")) {
            if (!region_json["start"].is_string()) {
                return PolicyErrorInfo::err(PolicyError::InvalidMemoryRegion,
                                            "'start' must be a hex string");
            }
            auto start = parse_hex(region_json["start"].as_string());
            if (!start) {
                return PolicyErrorInfo::err(PolicyError::InvalidMemoryRegion,
                                            "Invalid hex in 'start'");
            }
            cond.start = *start;
        }

        if (region_json.contains("end")) {
            if (!region_json["end"].is_string()) {
                return PolicyErrorInfo::err(PolicyError::InvalidMemoryRegion,
                                            "'end' must be a hex string");
            }
            auto end = parse_hex(region_json["end"].as_string());
            if (!end) {
                return PolicyErrorInfo::err(PolicyError::InvalidMemoryRegion,
                                            "Invalid hex in 'end'");
            }
            cond.end = *end;
        }

        conditions.push_back(cond);
    }

    // Parse time_window
    if (if_json.contains("time_window")) {
        const JsonValue& time_json = if_json["time_window"];
        if (!time_json.is_object()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'time_window' must be an object");
        }

        TimeWindowCondition cond;

        if (time_json.contains("after")) {
            if (!time_json["after"].is_string()) {
                return PolicyErrorInfo::err(PolicyError::InvalidTimeWindow,
                                            "'after' must be a time string");
            }
            auto after = parse_time(time_json["after"].as_string());
            if (!after) {
                return PolicyErrorInfo::err(PolicyError::InvalidTimeWindow,
                                            "Invalid time format in 'after'");
            }
            cond.after_minutes = *after;
        }

        if (time_json.contains("before")) {
            if (!time_json["before"].is_string()) {
                return PolicyErrorInfo::err(PolicyError::InvalidTimeWindow,
                                            "'before' must be a time string");
            }
            auto before = parse_time(time_json["before"].as_string());
            if (!before) {
                return PolicyErrorInfo::err(PolicyError::InvalidTimeWindow,
                                            "Invalid time format in 'before'");
            }
            cond.before_minutes = *before;
        }

        conditions.push_back(cond);
    }

    // Parse caller_chain
    if (if_json.contains("caller_chain")) {
        const JsonValue& chain_json = if_json["caller_chain"];
        if (!chain_json.is_object()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'caller_chain' must be an object");
        }

        CallerChainCondition cond;

        if (chain_json.contains("min_depth") && chain_json["min_depth"].is_int()) {
            cond.min_depth = static_cast<std::uint32_t>(chain_json["min_depth"].as_int());
        }

        if (chain_json.contains("max_depth") && chain_json["max_depth"].is_int()) {
            cond.max_depth = static_cast<std::uint32_t>(chain_json["max_depth"].as_int());
        }

        if (chain_json.contains("root_dot") && chain_json["root_dot"].is_int()) {
            cond.root_dot = static_cast<std::uint64_t>(chain_json["root_dot"].as_int());
        }

        conditions.push_back(cond);
    }

    // Parse resource_usage
    if (if_json.contains("resource_usage")) {
        const JsonValue& resource_json = if_json["resource_usage"];
        if (!resource_json.is_object()) {
            return PolicyErrorInfo::err(PolicyError::InvalidFieldType,
                                        "'resource_usage' must be an object");
        }

        ResourceUsageCondition cond;

        if (resource_json.contains("memory_above_percent") &&
            resource_json["memory_above_percent"].is_int()) {
            cond.memory_above_percent =
                static_cast<std::uint8_t>(resource_json["memory_above_percent"].as_int());
        }

        if (resource_json.contains("instructions_above") &&
            resource_json["instructions_above"].is_int()) {
            cond.instructions_above =
                static_cast<std::uint64_t>(resource_json["instructions_above"].as_int());
        }

        conditions.push_back(cond);
    }

    return conditions;
}

Result<RuleAction, PolicyErrorInfo> PolicyEngine::parse_action(const JsonValue& then_json) {
    if (!then_json.is_object()) {
        return PolicyErrorInfo::err(PolicyError::InvalidFieldType, "'then' must be an object");
    }

    RuleAction action;

    // Parse action (required)
    if (!then_json.contains("action")) {
        return PolicyErrorInfo::err(PolicyError::MissingField, "'then' missing 'action' field");
    }

    const JsonValue& action_json = then_json["action"];
    if (!action_json.is_string()) {
        return PolicyErrorInfo::err(PolicyError::InvalidFieldType, "'action' must be a string");
    }

    auto it = ACTION_MAP.find(action_json.as_string());
    if (it == ACTION_MAP.end()) {
        return PolicyErrorInfo::err(PolicyError::InvalidAction,
                                    "Unknown action: " + action_json.as_string());
    }
    action.decision = it->second;

    // Parse capability (for RequireCapability)
    if (then_json.contains("capability") && then_json["capability"].is_string()) {
        action.capability = then_json["capability"].as_string();
    }

    // Parse reason (for Audit/Deny)
    if (then_json.contains("reason") && then_json["reason"].is_string()) {
        action.reason = then_json["reason"].as_string();
    }

    return action;
}

std::optional<std::uint8_t> PolicyEngine::parse_opcode_name(std::string_view name) {
    auto it = OPCODE_MAP.find(name);
    if (it != OPCODE_MAP.end()) {
        return it->second;
    }

    // Try parsing as numeric value
    std::uint8_t value = 0;
    auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), value);
    if (ec == std::errc{} && ptr == name.data() + name.size()) {
        return value;
    }

    return std::nullopt;
}

std::optional<std::uint16_t> PolicyEngine::parse_time(std::string_view time_str) {
    // Format: "HH:MM"
    if (time_str.size() != 5 || time_str[2] != ':') {
        return std::nullopt;
    }

    std::uint32_t hours = 0;
    std::uint32_t minutes = 0;

    auto [ptr1, ec1] = std::from_chars(time_str.data(), time_str.data() + 2, hours);
    if (ec1 != std::errc{} || ptr1 != time_str.data() + 2) {
        return std::nullopt;
    }

    auto [ptr2, ec2] = std::from_chars(time_str.data() + 3, time_str.data() + 5, minutes);
    if (ec2 != std::errc{} || ptr2 != time_str.data() + 5) {
        return std::nullopt;
    }

    if (hours > 23 || minutes > 59) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(hours * 60 + minutes);
}

std::optional<std::uint64_t> PolicyEngine::parse_hex(std::string_view hex_str) {
    // Strip optional "0x" prefix
    if (hex_str.starts_with("0x") || hex_str.starts_with("0X")) {
        hex_str = hex_str.substr(2);
    }

    if (hex_str.empty()) {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(hex_str.data(), hex_str.data() + hex_str.size(), value, 16);
    if (ec != std::errc{} || ptr != hex_str.data() + hex_str.size()) {
        return std::nullopt;
    }

    return value;
}

}  // namespace dotvm::core::policy

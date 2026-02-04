#pragma once
#include <nlohmann/json.hpp>

// Base interface for serializable objects
struct ISerializable {
    virtual ~ISerializable() = default;
    virtual nlohmann::json toJson() const = 0;
    virtual void fromJson(const nlohmann::json& j) = 0;
};
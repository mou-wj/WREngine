#pragma once
#include "Math.hpp"
#include <functional>
#include <string>
#include <utility>
#include <variant>

namespace Engine
{
    enum class EComponentPropertyType { Boolean, Scalar, Vector3, Color3, Text };
    enum class EComponentPropertySemantic { None, Position, Rotation, Scale };
    using ComponentPropertyValue = std::variant<bool,float,Core::Float3,std::string>;

    // A control-thread view of a live component. Callbacks borrow that component;
    // collect, read and write within the current operation, without retaining it.
    struct ComponentProperty
    {
        std::string Id;
        std::string Label;
        std::string Category;
        std::string Tooltip;
        EComponentPropertyType Type = EComponentPropertyType::Boolean;
        EComponentPropertySemantic Semantic = EComponentPropertySemantic::None;
        float Speed = 0.05f;
        float Minimum = 0;
        float Maximum = 0;
        bool bReadOnly = false;
        std::function<ComponentPropertyValue()> Read;
        // Return false when a candidate value is invalid and was not applied.
        std::function<bool(const ComponentPropertyValue&)> Write;
    };

    template<typename T,typename Getter>
    ComponentProperty MakeComponentProperty(const char* id,const char* label,const char* category,
        EComponentPropertyType type,Getter read,std::function<bool(const T&)> write = {})
    {
        ComponentProperty property;
        property.Id=id; property.Label=label; property.Category=category; property.Type=type;
        property.Read=[read=std::move(read)] { return ComponentPropertyValue(T(read())); };
        property.bReadOnly=!write;
        if (write) property.Write=[write=std::move(write)](const ComponentPropertyValue& value) {
            const auto* typed=std::get_if<T>(&value); return typed && write(*typed);
        };
        return property;
    }
}

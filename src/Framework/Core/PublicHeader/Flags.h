#pragma once
template<typename EnumType>
class TFlags
{
public:
    using StorageType = std::underlying_type_t<EnumType>;

    constexpr TFlags() = default;
    constexpr TFlags(EnumType value) : m_Bits(static_cast<StorageType>(value)) {}
    explicit constexpr TFlags(StorageType value) : m_Bits(value) {}

    constexpr StorageType GetBits() const { return m_Bits; }
    constexpr bool HasAny(TFlags rhs) const { return (m_Bits & rhs.m_Bits) != 0; }
    constexpr bool HasAll(TFlags rhs) const { return (m_Bits & rhs.m_Bits) == rhs.m_Bits; }
    constexpr bool IsEmpty() const { return m_Bits == 0; }
    constexpr EnumType ToEnum() const { return static_cast<EnumType>(m_Bits); }

    constexpr TFlags operator|(TFlags rhs) const { return TFlags(static_cast<StorageType>(m_Bits | rhs.m_Bits)); }
    constexpr TFlags operator&(TFlags rhs) const { return TFlags(static_cast<StorageType>(m_Bits & rhs.m_Bits)); }
    constexpr TFlags operator~() const { return TFlags(static_cast<StorageType>(~m_Bits)); }

    constexpr TFlags& operator|=(TFlags rhs)
    {
        m_Bits = static_cast<StorageType>(m_Bits | rhs.m_Bits);
        return *this;
    }

    constexpr TFlags& operator&=(TFlags rhs)
    {
        m_Bits = static_cast<StorageType>(m_Bits & rhs.m_Bits);
        return *this;
    }


    constexpr bool operator==(TFlags rhs) const
    {
        return m_Bits == rhs.m_Bits;
    }

    constexpr bool operator!=(TFlags rhs) const
    {
        return m_Bits != rhs.m_Bits;
    }

private:
    StorageType m_Bits = 0;
};

template<typename EnumType>
inline constexpr bool EnumHasAnyFlags(TFlags<EnumType> flags, TFlags<EnumType> contains)
{
    return flags.HasAny(contains);
}

template<typename EnumType>
inline constexpr bool EnumHasAnyFlags(EnumType flags, EnumType contains)
{
    return EnumHasAnyFlags(TFlags<EnumType>(flags), TFlags<EnumType>(contains));
}

template<typename EnumType>
inline constexpr bool EnumHasAnyFlags(EnumType flags, TFlags<EnumType> contains)
{
    return EnumHasAnyFlags(TFlags<EnumType>(flags), contains);
}

template<typename EnumType>
inline constexpr bool EnumHasAnyFlags(TFlags<EnumType> flags, EnumType contains)
{
    return EnumHasAnyFlags(flags, TFlags<EnumType>(contains));
}

#define ENUM_CLASS_FLAGS(Enum, FlagsType) \
    using FlagsType = TFlags<Enum>; \
    inline constexpr FlagsType operator|(Enum a, Enum b) { return FlagsType(a) | FlagsType(b); } \
    inline constexpr FlagsType operator&(Enum a, Enum b) { return FlagsType(a) & FlagsType(b); } \
    inline constexpr FlagsType operator~(Enum a) { return ~FlagsType(a); } \
    inline constexpr FlagsType operator|(FlagsType a, Enum b) { return a | FlagsType(b); } \
    inline constexpr FlagsType operator&(FlagsType a, Enum b) { return a & FlagsType(b); } \
    inline constexpr FlagsType operator|(Enum a, FlagsType b) { return FlagsType(a) | b; } \
    inline constexpr FlagsType operator&(Enum a, FlagsType b) { return FlagsType(a) & b; } 


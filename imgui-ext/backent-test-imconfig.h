#pragma once
#if 0
#include <cstddef>
#include <type_traits>

// Helper Macros - IM_CONSTEXPR_11, IM_CONSTEXPR_14: Expands to 'constexpr' according to C++11 or C++14 rules.
#if (defined(_MSC_VER) && (_MSC_VER > 1900)) || (defined(__cplusplus) && (__cplusplus >= 201103L))
#define IM_CONSTEXPR_11 constexpr
#define IM_CONSTEXPR_14 constexpr
#elif defined(_MSC_VER) && (_MSC_VER == 1900)
#define IM_CONSTEXPR_11 constexpr
#define IM_CONSTEXPR_14
#else
#define IM_CONSTEXPR_11
#define IM_CONSTEXPR_14
#endif

// Safe type
template <typename T>
struct ImUnderlyingType { using Type = int; };

template <typename T, typename Tag>
struct ImSafeType
{
protected:
    T _Value;
    IM_CONSTEXPR_11 explicit ImSafeType(T value): _Value(value) {}
public:
    IM_CONSTEXPR_11 ImSafeType(const ImSafeType& other): _Value(other._Value) {}
    IM_CONSTEXPR_14 ImSafeType& operator=(const ImSafeType& other) { _Value = other._Value; return *this; }
};

template <typename T>
struct ImEnableSafeEnumConversion{ enum { Value = 0 }; };

template <typename T, typename Tag, typename Enum, T Sentinel>
struct ImSafeEnum: ImSafeType<T, Tag>
{
    using Base = ImSafeType<T, Tag>;

    //using Base::Base;
    //using Base::operator=;

    IM_CONSTEXPR_11 ImSafeEnum(): ImSafeEnum(Sentinel) {}
    IM_CONSTEXPR_11 ImSafeEnum(Enum value): Base((T)value) {}
    IM_CONSTEXPR_11 ImSafeEnum(std::nullptr_t): Base(Sentinel) {}
    template <typename U>
    IM_CONSTEXPR_11 ImSafeEnum(U value, std::enable_if_t<ImEnableSafeEnumConversion<U>::Value>* = nullptr): Base((T)value) {}

    IM_CONSTEXPR_11 explicit ImSafeEnum(T value): Base(value) {}
    IM_CONSTEXPR_11 explicit operator T() const { return this->_Value; }
    IM_CONSTEXPR_11 explicit operator Enum() const { return (Enum)this->_Value; }
    //IM_CONSTEXPR_11 explicit operator bool() const { return this->_Value != Sentinel; }

    friend inline IM_CONSTEXPR_11 bool operator==(ImSafeEnum lhs, ImSafeEnum rhs) { return lhs._Value == rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator!=(ImSafeEnum lhs, ImSafeEnum rhs) { return lhs._Value != rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator< (ImSafeEnum lhs, ImSafeEnum rhs) { return lhs._Value <  rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator> (ImSafeEnum lhs, ImSafeEnum rhs) { return lhs._Value >  rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator<=(ImSafeEnum lhs, ImSafeEnum rhs) { return lhs._Value <= rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator>=(ImSafeEnum lhs, ImSafeEnum rhs) { return lhs._Value >= rhs._Value; }

    friend inline IM_CONSTEXPR_11 bool operator==(Enum lhs, ImSafeEnum rhs) { return (T)lhs == rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator!=(Enum lhs, ImSafeEnum rhs) { return (T)lhs != rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator< (Enum lhs, ImSafeEnum rhs) { return (T)lhs <  rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator> (Enum lhs, ImSafeEnum rhs) { return (T)lhs >  rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator<=(Enum lhs, ImSafeEnum rhs) { return (T)lhs <= rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator>=(Enum lhs, ImSafeEnum rhs) { return (T)lhs >= rhs._Value; }

    friend inline IM_CONSTEXPR_11 bool operator==(ImSafeEnum lhs, Enum rhs) { return lhs._Value == (T)rhs; }
    friend inline IM_CONSTEXPR_11 bool operator!=(ImSafeEnum lhs, Enum rhs) { return lhs._Value != (T)rhs; }
    friend inline IM_CONSTEXPR_11 bool operator< (ImSafeEnum lhs, Enum rhs) { return lhs._Value <  (T)rhs; }
    friend inline IM_CONSTEXPR_11 bool operator> (ImSafeEnum lhs, Enum rhs) { return lhs._Value >  (T)rhs; }
    friend inline IM_CONSTEXPR_11 bool operator<=(ImSafeEnum lhs, Enum rhs) { return lhs._Value <= (T)rhs; }
    friend inline IM_CONSTEXPR_11 bool operator>=(ImSafeEnum lhs, Enum rhs) { return lhs._Value >= (T)rhs; }

    friend inline IM_CONSTEXPR_11 bool operator< (T lhs, ImSafeEnum rhs) { return lhs <  rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator> (T lhs, ImSafeEnum rhs) { return lhs >  rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator<=(T lhs, ImSafeEnum rhs) { return lhs <= rhs._Value; }
    friend inline IM_CONSTEXPR_11 bool operator>=(T lhs, ImSafeEnum rhs) { return lhs >= rhs._Value; }

    friend inline IM_CONSTEXPR_11 bool operator< (ImSafeEnum lhs, T rhs) { return lhs._Value <  rhs; }
    friend inline IM_CONSTEXPR_11 bool operator> (ImSafeEnum lhs, T rhs) { return lhs._Value >  rhs; }
    friend inline IM_CONSTEXPR_11 bool operator<=(ImSafeEnum lhs, T rhs) { return lhs._Value <= rhs; }
    friend inline IM_CONSTEXPR_11 bool operator>=(ImSafeEnum lhs, T rhs) { return lhs._Value >= rhs; }

    //IM_CONSTEXPR_14 ImSafeEnum& operator=(Enum value) { this->_Value = (T)value; }
    //IM_CONSTEXPR_14 ImSafeEnum& operator=(std::nullptr_t) { this->_Value = Sentinel; }
};

template <typename T, typename Tag, typename Enum, T Sentinel>
struct ImSafeFlags: ImSafeEnum<T, Tag, Enum, Sentinel>
{
    using Base = ImSafeEnum<T, Tag, Enum, Sentinel>;

    using Base::Base;

          T* AsPointer()       { return &this->_Value; }
    const T* AsPointer() const { return &this->_Value; }

    IM_CONSTEXPR_11 explicit operator bool() const { return this->_Value != 0; }

    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator| (ImSafeFlags lhs, ImSafeFlags rhs) { return (ImSafeFlags)(lhs._Value | rhs._Value); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator& (ImSafeFlags lhs, ImSafeFlags rhs) { return (ImSafeFlags)(lhs._Value & rhs._Value); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator^ (ImSafeFlags lhs, ImSafeFlags rhs) { return (ImSafeFlags)(lhs._Value ^ rhs._Value); }

    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator| (Enum lhs, ImSafeFlags rhs) { return (ImSafeFlags)((T)lhs | rhs._Value); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator& (Enum lhs, ImSafeFlags rhs) { return (ImSafeFlags)((T)lhs & rhs._Value); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator^ (Enum lhs, ImSafeFlags rhs) { return (ImSafeFlags)((T)lhs ^ rhs._Value); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator| (T lhs, ImSafeFlags rhs) { return (ImSafeFlags)(lhs | rhs._Value); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator& (T lhs, ImSafeFlags rhs) { return (ImSafeFlags)(lhs & rhs._Value); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags  operator^ (T lhs, ImSafeFlags rhs) { return (ImSafeFlags)(lhs ^ rhs._Value); }

    friend inline IM_CONSTEXPR_11 ImSafeFlags operator|(ImSafeFlags lhs, Enum rhs) { return (ImSafeFlags)(lhs._Value | (T)rhs); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags operator&(ImSafeFlags lhs, Enum rhs) { return (ImSafeFlags)(lhs._Value & (T)rhs); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags operator^(ImSafeFlags lhs, Enum rhs) { return (ImSafeFlags)(lhs._Value ^ (T)rhs); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags operator|(ImSafeFlags lhs, T rhs) { return (ImSafeFlags)(lhs._Value | rhs); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags operator&(ImSafeFlags lhs, T rhs) { return (ImSafeFlags)(lhs._Value & rhs); }
    friend inline IM_CONSTEXPR_11 ImSafeFlags operator^(ImSafeFlags lhs, T rhs) { return (ImSafeFlags)(lhs._Value ^ rhs); }

    inline IM_CONSTEXPR_11 ImSafeFlags& operator|=(ImSafeFlags rhs) { this->_Value = this->_Value | rhs._Value; return *this; }
    inline IM_CONSTEXPR_11 ImSafeFlags& operator&=(ImSafeFlags rhs) { this->_Value = this->_Value & rhs._Value; return *this; }
    inline IM_CONSTEXPR_11 ImSafeFlags& operator^=(ImSafeFlags rhs) { this->_Value = this->_Value ^ rhs._Value; return *this; }
    inline IM_CONSTEXPR_11 ImSafeFlags& operator|=(Enum rhs)        { this->_Value = this->_Value | (T)rhs; return *this; }
    inline IM_CONSTEXPR_11 ImSafeFlags& operator&=(Enum rhs)        { this->_Value = this->_Value & (T)rhs; return *this; }
    inline IM_CONSTEXPR_11 ImSafeFlags& operator^=(Enum rhs)        { this->_Value = this->_Value ^ (T)rhs; return *this; }
    inline IM_CONSTEXPR_11 ImSafeFlags& operator|=(T rhs)           { this->_Value = this->_Value | rhs; return *this; }
    inline IM_CONSTEXPR_11 ImSafeFlags& operator&=(T rhs)           { this->_Value = this->_Value & rhs; return *this; }
    inline IM_CONSTEXPR_11 ImSafeFlags& operator^=(T rhs)           { this->_Value = this->_Value ^ rhs; return *this; }
};

#define IM_DECLARE_ENUM(type, name) \
enum name##_ : type; \
template <> \
struct ImUnderlyingType<name##_> { using Type = type; }; \
using name = ImSafeEnum<type, name##_, name##_, 0>

#define IM_DEFINE_ENUM(name) \
enum name : typename ImUnderlyingType<name>::Type

#define IM_PRIVATE_ENUM(name, base) \
enum name : typename ImUnderlyingType<base##_>::Type; \
template <> \
struct ImEnableSafeEnumConversion<name>{ enum { Value = 1 }; }; \
inline IM_CONSTEXPR_11 bool operator==(name lhs, base##_ rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs == (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator!=(name lhs, base##_ rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs != (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator< (name lhs, base##_ rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs <  (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator> (name lhs, base##_ rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs >  (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator<=(name lhs, base##_ rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs <= (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator>=(name lhs, base##_ rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs >= (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator==(base##_ lhs, name rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs == (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator!=(base##_ lhs, name rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs != (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator< (base##_ lhs, name rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs <  (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator> (base##_ lhs, name rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs >  (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator<=(base##_ lhs, name rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs <= (typename ImUnderlyingType<base##_>::Type)rhs; } \
inline IM_CONSTEXPR_11 bool operator>=(base##_ lhs, name rhs) { return (typename ImUnderlyingType<base##_>::Type)lhs >= (typename ImUnderlyingType<base##_>::Type)rhs; } \
enum name : typename ImUnderlyingType<base##_>::Type



//#define IM_DECLARE_FLAGS(type, name) \
//enum name##_ : type; \
//template <> \
//struct ImUnderlyingType<name##_> { using Type = type; }; \
//using name = ImSafeFlags<type, name##_, name##_, 0>

//#define IM_DEFINE_FLAGS(name) \
//enum name : typename ImUnderlyingType<name>::Type

#endif
#include <DataTypes/DataTypesDecimal.h>
#include <DataTypes/Serializations/SerializationDecimal.h>

#include <Common/typeid_cast.h>
#include <Core/DecimalFunctions.h>
#include <DataTypes/DataTypeFactory.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>
#include <IO/readDecimalText.h>
#include <Parsers/ASTLiteral.h>

#include <type_traits>

namespace DB
{

namespace ErrorCodes
{
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int DECIMAL_OVERFLOW;
}


template <is_decimal T>
std::string DataTypeDecimal<T>::doGetName() const
{
    return fmt::format("Decimal({}, {})", this->precision, this->scale);
}

template <is_decimal T>
std::string DataTypeDecimal<T>::getSQLCompatibleName() const
{
    /// See https://dev.mysql.com/doc/refman/8.0/en/precision-math-decimal-characteristics.html
    /// DECIMAL(M,D)
    /// M is the maximum number of digits (the precision). It has a range of 1 to 65.
    /// D is the number of digits to the right of the decimal point (the scale). It has a range of 0 to 30 and must be no larger than M.
    if (this->precision > 65 || this->scale > 30)
        return "TEXT";
    return fmt::format("DECIMAL({}, {})", this->precision, this->scale);
}

template <is_decimal T>
bool DataTypeDecimal<T>::equals(const IDataType & rhs) const
{
    if (auto * ptype = typeid_cast<const DataTypeDecimal<T> *>(&rhs))
        return this->scale == ptype->getScale();
    return false;
}

template <is_decimal T>
DataTypePtr DataTypeDecimal<T>::promoteNumericType() const
{
    if (sizeof(T) <= sizeof(Decimal128))
        return std::make_shared<DataTypeDecimal<Decimal128>>(DataTypeDecimal<Decimal128>::maxPrecision(), this->scale);
    else
        return std::make_shared<DataTypeDecimal<Decimal256>>(DataTypeDecimal<Decimal256>::maxPrecision(), this->scale);
}

template <is_decimal T>
T DataTypeDecimal<T>::parseFromString(const String & str) const
{
    ReadBufferFromMemory buf(str.data(), str.size());
    T x;
    UInt32 unread_scale = this->scale;
    readDecimalText(buf, x, this->precision, unread_scale, true);

    if (common::mulOverflow(x.value, DecimalUtils::scaleMultiplier<T>(unread_scale), x.value))
        throw Exception(ErrorCodes::DECIMAL_OVERFLOW, "Decimal math overflow");

    return x;
}

template <is_decimal T>
SerializationPtr DataTypeDecimal<T>::doGetDefaultSerialization() const
{
    return std::make_shared<SerializationDecimal<T>>(this->precision, this->scale);
}


static DataTypePtr create(const ASTPtr & arguments)
{
    UInt64 precision = 10;
    UInt64 scale = 0;
    if (arguments)
    {
        if (arguments->children.empty() || arguments->children.size() > 2)
            throw Exception(
                ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH,
                "Decimal data type family must have precision and optional scale arguments");

        const auto * precision_arg = arguments->children[0]->as<ASTLiteral>();
        if (!precision_arg || precision_arg->value.getType() != Field::Types::UInt64)
            throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Decimal argument precision is invalid");
        precision = precision_arg->value.get<UInt64>();

        if (arguments->children.size() == 2)
        {
            const auto * scale_arg = arguments->children[1]->as<ASTLiteral>();
            if (!scale_arg || !isInt64OrUInt64FieldType(scale_arg->value.getType()))
                throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Decimal argument scale is invalid");
            scale = scale_arg->value.get<UInt64>();
        }
    }

    return createDecimal<DataTypeDecimal>(precision, scale);
}

template <typename T>
static DataTypePtr createExact(const ASTPtr & arguments)
{
    if (!arguments || arguments->children.size() != 1)
        throw Exception(ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH,
        "Decimal32 | Decimal64 | Decimal128 | Decimal256 data type family must have exactly one arguments: scale");
    const auto * scale_arg = arguments->children[0]->as<ASTLiteral>();

    if (!scale_arg || !(scale_arg->value.getType() == Field::Types::Int64 || scale_arg->value.getType() == Field::Types::UInt64))
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
        "Decimal32 | Decimal64 | Decimal128 | Decimal256 data type family must have a one number as its argument");

    UInt64 precision = DecimalUtils::max_precision<T>;
    UInt64 scale = scale_arg->value.get<UInt64>();

    return createDecimal<DataTypeDecimal>(precision, scale);
}

void registerDataTypeDecimal(DataTypeFactory & factory)
{
    factory.registerDataType("Decimal32", createExact<Decimal32>, DataTypeFactory::CaseInsensitive);
    factory.registerDataType("Decimal64", createExact<Decimal64>, DataTypeFactory::CaseInsensitive);
    factory.registerDataType("Decimal128", createExact<Decimal128>, DataTypeFactory::CaseInsensitive);
    factory.registerDataType("Decimal256", createExact<Decimal256>, DataTypeFactory::CaseInsensitive);

    factory.registerDataType("Decimal", create, DataTypeFactory::CaseInsensitive);
    factory.registerAlias("DEC", "Decimal", DataTypeFactory::CaseInsensitive);
    factory.registerAlias("NUMERIC", "Decimal", DataTypeFactory::CaseInsensitive);
    factory.registerAlias("FIXED", "Decimal", DataTypeFactory::CaseInsensitive);
}

/// Explicit template instantiations.
template class DataTypeDecimal<Decimal32>;
template class DataTypeDecimal<Decimal64>;
template class DataTypeDecimal<Decimal128>;
template class DataTypeDecimal<Decimal256>;

}

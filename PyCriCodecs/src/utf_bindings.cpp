#include "binding_helpers.hpp"

#include <limits>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "../../CriCodecs/src/utf/utf_table.hpp"
#include "../../CriCodecs/src/utilities/text_encoding.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] std::string raw_cri_string_from_python(const nb::handle& value, const nb::handle& encoding);

[[nodiscard]] cricodecs::utf::UtfTable load_utf_path(const std::string& path) {
    return unwrap_expected(cricodecs::utf::UtfTable::load(std::filesystem::path(path)));
}

[[nodiscard]] cricodecs::utf::UtfTable load_utf_bytes(const nb::bytes& data) {
    auto borrowed = borrow_python_source(data);
    const auto view = borrowed.as_span();
    auto owner = keep_python_bytes_alive(std::move(borrowed));
    return unwrap_expected(cricodecs::utf::UtfTable::load(view, std::move(owner)));
}

[[nodiscard]] cricodecs::utf::UtfTable load_utf_source(const nb::object& source) {
    auto borrowed = borrow_python_source(source);
    const auto view = borrowed.as_span();
    auto owner = keep_python_bytes_alive(std::move(borrowed));
    return unwrap_expected(cricodecs::utf::UtfTable::load(view, std::move(owner)));
}

[[nodiscard]] cricodecs::utf::UtfTable load_utf_any(const nb::object& source) {
    if (auto path = python_text_path(source)) {
        return load_utf_path(*path);
    }
    return load_utf_source(source);
}

[[nodiscard]] cricodecs::utf::UtfTable load_utf_any_with_encoding(
    const nb::object& source,
    const nb::object& encoding
) {
    auto table = load_utf_any(source);
    if (encoding.is_none()) {
        table.set_text_encoding(std::nullopt);
    } else {
        table.set_text_encoding(nb::cast<std::string>(encoding));
    }
    return table;
}

[[nodiscard]] cricodecs::utf::UtfTable create_utf_table(
    const nb::object& table_name,
    uint16_t version,
    const nb::object& encoding
) {
    const auto raw_name = raw_cri_string_from_python(table_name, encoding);
    return cricodecs::utf::UtfTable::create(raw_name, version);
}

[[nodiscard]] uint32_t checked_column_index(const cricodecs::utf::UtfTable& table, uint32_t index) {
    if (index >= table.column_count()) {
        raise_value_error("UTF column index is out of range");
    }
    return index;
}

[[nodiscard]] uint32_t checked_row_index(const cricodecs::utf::UtfTable& table, uint32_t index) {
    if (index >= table.row_count()) {
        raise_value_error("UTF row index is out of range");
    }
    return index;
}

[[nodiscard]] uint32_t checked_column_index(const cricodecs::utf::UtfTable& table, const std::string& name) {
    const int index = table.find_column(name);
    if (index < 0) {
        raise_value_error("UTF column not found: " + name);
    }
    return static_cast<uint32_t>(index);
}

[[nodiscard]] uint8_t column_flag_bits_from_python(const nb::handle& flag) {
    if (PyLong_Check(flag.ptr())) {
        const auto bits = nb::cast<uint64_t>(flag);
        if (bits > std::numeric_limits<uint8_t>::max()) {
            raise_value_error("UTF column flag is out of range");
        }
        return static_cast<uint8_t>(bits);
    }

    if (PyTuple_Check(flag.ptr()) || PyList_Check(flag.ptr())) {
        uint8_t bits = 0;
        const auto size = PySequence_Size(flag.ptr());
        if (size < 0) {
            throw nb::python_error();
        }
        for (Py_ssize_t index = 0; index < size; ++index) {
            auto item = nb::steal<nb::object>(PySequence_GetItem(flag.ptr(), index));
            bits = static_cast<uint8_t>(bits | column_flag_bits_from_python(item));
        }
        return bits;
    }

    try {
        return static_cast<uint8_t>(nb::cast<cricodecs::utf::ColumnFlag>(flag));
    } catch (const nb::cast_error&) {
        raise_type_error("unsupported UTF column flag value");
    }
}

[[nodiscard]] cricodecs::text::EncodingOptions encoding_options_from_python(const nb::handle& encoding) {
    if (encoding.is_none()) {
        return {};
    }

    std::string name = nb::cast<std::string>(encoding);
    cricodecs::text::EncodingOptions options{std::move(name)};
    if (options.encoding->empty() || cricodecs::text::is_auto_encoding(options)) {
        return {};
    }
    return options;
}

[[nodiscard]] nb::object effective_encoding_object(
    const cricodecs::utf::UtfTable& table,
    const nb::handle& encoding
) {
    if (!encoding.is_none()) {
        return nb::borrow<nb::object>(encoding);
    }
    if (table.text_encoding().has_value()) {
        const auto& value = *table.text_encoding();
        return nb::str(value.data(), value.size());
    }
    return nb::none();
}

void set_text_encoding_from_python(cricodecs::utf::UtfTable& table, const nb::handle& encoding) {
    if (encoding.is_none()) {
        table.set_text_encoding(std::nullopt);
        return;
    }
    table.set_text_encoding(nb::cast<std::string>(encoding));
}

[[nodiscard]] nb::bytes string_view_to_python_bytes(std::string_view value) {
    return to_python_bytes(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(value.data()),
        value.size()
    ));
}

[[nodiscard]] std::string decode_cri_text(std::string_view bytes, const nb::handle& encoding) {
    auto decoded = cricodecs::text::decode_to_utf8(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()),
        encoding_options_from_python(encoding)
    );
    if (!decoded) {
        raise_value_error(decoded.error());
    }
    return *decoded;
}

[[nodiscard]] std::string raw_cri_string_from_python(const nb::handle& value, const nb::handle& encoding) {
    if (nb::isinstance<nb::bytes>(value)) {
        std::string raw = std::string(borrow_python_bytes(nb::cast<nb::bytes>(value)));
        if (cricodecs::text::contains_nul(raw)) {
            raise_value_error("CRI UTF strings cannot contain embedded NUL bytes");
        }
        return raw;
    }

    const std::string text = nb::cast<std::string>(value);
    auto encoded = cricodecs::text::encode_cri_string(text, encoding_options_from_python(encoding));
    if (!encoded) {
        raise_value_error(encoded.error());
    }
    return std::string(encoded->begin(), encoded->end());
}

[[nodiscard]] uint32_t checked_column_key(
    const cricodecs::utf::UtfTable& table,
    const nb::handle& column,
    const nb::handle& encoding
) {
    if (PyLong_Check(column.ptr())) {
        return checked_column_index(table, nb::cast<uint32_t>(column));
    }

    const auto raw_name = raw_cri_string_from_python(column, encoding);
    return checked_column_index(table, raw_name);
}

[[nodiscard]] const cricodecs::utf::Column& checked_column_at(
    const cricodecs::utf::UtfTable& table,
    uint32_t index
) {
    return table.column(checked_column_index(table, index));
}

[[nodiscard]] cricodecs::utf::GUID guid_from_python_bytes(const nb::bytes& value) {
    const auto bytes = copy_python_bytes(value);
    if (bytes.size() != 16) {
        raise_value_error("UTF GUID values must be exactly 16 bytes");
    }

    cricodecs::utf::GUID guid{};
    std::ranges::copy(bytes, guid.data);
    return guid;
}

template <typename Int>
[[nodiscard]] Int checked_integer_cast(const nb::handle& value, std::string_view context) {
    using Limits = std::numeric_limits<Int>;
    if constexpr (std::is_signed_v<Int>) {
        const int64_t converted = nb::cast<int64_t>(value);
        if (converted < static_cast<int64_t>(Limits::lowest()) || converted > static_cast<int64_t>(Limits::max())) {
            raise_value_error(std::string(context) + " is out of range");
        }
        return static_cast<Int>(converted);
    } else {
        const uint64_t converted = nb::cast<uint64_t>(value);
        if (converted > static_cast<uint64_t>(Limits::max())) {
            raise_value_error(std::string(context) + " is out of range");
        }
        return static_cast<Int>(converted);
    }
}

[[nodiscard]] cricodecs::utf::Value python_to_utf_value(
    const nb::handle& value,
    cricodecs::utf::ColumnType column_type,
    const nb::handle& encoding
) {
    if (value.is_none()) {
        return std::monostate{};
    }

    switch (column_type) {
        case cricodecs::utf::ColumnType::UInt8:
            return checked_integer_cast<uint8_t>(value, "UTF value");
        case cricodecs::utf::ColumnType::SInt8:
            return checked_integer_cast<int8_t>(value, "UTF value");
        case cricodecs::utf::ColumnType::UInt16:
            return checked_integer_cast<uint16_t>(value, "UTF value");
        case cricodecs::utf::ColumnType::SInt16:
            return checked_integer_cast<int16_t>(value, "UTF value");
        case cricodecs::utf::ColumnType::UInt32:
            return checked_integer_cast<uint32_t>(value, "UTF value");
        case cricodecs::utf::ColumnType::SInt32:
            return checked_integer_cast<int32_t>(value, "UTF value");
        case cricodecs::utf::ColumnType::UInt64:
            return checked_integer_cast<uint64_t>(value, "UTF value");
        case cricodecs::utf::ColumnType::SInt64:
            return checked_integer_cast<int64_t>(value, "UTF value");
        case cricodecs::utf::ColumnType::Float:
            return nb::cast<float>(value);
        case cricodecs::utf::ColumnType::Double:
            return nb::cast<double>(value);
        case cricodecs::utf::ColumnType::String:
            return raw_cri_string_from_python(value, encoding);
        case cricodecs::utf::ColumnType::VLData: {
            if (nb::isinstance<nb::bytes>(value)) {
                return copy_python_bytes(nb::cast<nb::bytes>(value));
            }
            return nb::cast<cricodecs::utf::DataRef>(value);
        }
        case cricodecs::utf::ColumnType::GUID: {
            if (nb::isinstance<nb::bytes>(value)) {
                return guid_from_python_bytes(nb::cast<nb::bytes>(value));
            }
            return nb::cast<cricodecs::utf::GUID>(value);
        }
        default:
            raise_value_error("UTF value conversion failed: unsupported column type");
    }
}

[[nodiscard]] nb::object value_to_python(const cricodecs::utf::Value& value, const nb::handle& encoding) {
    return std::visit([&](const auto& item) -> nb::object {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return nb::none();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return nb::cast(decode_cri_text(item, encoding));
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return to_python_bytes(std::span<const uint8_t>(item.data(), item.size()));
        } else {
            return nb::cast(item);
        }
    }, value);
}

[[nodiscard]] nb::object value_to_python_raw(const cricodecs::utf::Value& value) {
    return std::visit([](const auto& item) -> nb::object {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return nb::none();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return string_view_to_python_bytes(item);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return to_python_bytes(std::span<const uint8_t>(item.data(), item.size()));
        } else {
            return nb::cast(item);
        }
    }, value);
}

[[nodiscard]] cricodecs::utf::Value materialized_value(
    const cricodecs::utf::UtfTable& table,
    uint32_t row,
    uint32_t column,
    const cricodecs::utf::Column& column_info
) {
    if (column_info.type == cricodecs::utf::ColumnType::VLData) {
        auto data = table.get_data(row, column);
        if (!data) {
            return std::monostate{};
        }
        return std::vector<uint8_t>(data->begin(), data->end());
    }
    return unwrap_expected(table.get_value(row, column));
}

[[nodiscard]] cricodecs::utf::Value materialized_default_value(
    const cricodecs::utf::UtfTable& table,
    uint32_t column,
    const cricodecs::utf::Column& column_info
) {
    if (column_info.type == cricodecs::utf::ColumnType::VLData) {
        auto data = table.get_default_data(column);
        if (!data) {
            return std::monostate{};
        }
        return std::vector<uint8_t>(data->begin(), data->end());
    }
    return unwrap_expected(table.get_default_value(column));
}

[[nodiscard]] cricodecs::utf::UtfTable editable_utf_copy(const cricodecs::utf::UtfTable& table) {
    auto editable = cricodecs::utf::UtfTable::create(table.table_name_bytes(), table.version());
    editable.set_text_encoding(table.text_encoding());
    if (table.row_width() != 0) {
        editable.set_row_width(table.row_width());
    }
    if (table.data_alignment() != 0) {
        editable.set_data_alignment(table.data_alignment());
    }

    for (uint32_t column = 0; column < table.column_count(); ++column) {
        const auto& column_info = table.column(column);
        editable.add_column(column_info.name, column_info.type, column_info.flag);
        if (cricodecs::utf::has_flag(column_info.flag, cricodecs::utf::ColumnFlag::Default)) {
            editable.set_default_value(column, materialized_default_value(table, column, column_info));
        }
    }

    for (uint32_t row = 0; row < table.row_count(); ++row) {
        editable.add_row();
        for (uint32_t column = 0; column < table.column_count(); ++column) {
            const auto& column_info = table.column(column);
            if (!cricodecs::utf::has_flag(column_info.flag, cricodecs::utf::ColumnFlag::Row)) {
                continue;
            }
            editable.set(row, column, materialized_value(table, row, column, column_info));
        }
    }

    return editable;
}

void ensure_editable(cricodecs::utf::UtfTable& table) {
    if (table.is_loaded()) {
        table = editable_utf_copy(table);
    }
}

[[nodiscard]] nb::object table_value(
    const cricodecs::utf::UtfTable& table,
    uint32_t row,
    const nb::object& column,
    const nb::object& encoding
) {
    const auto index = checked_column_key(table, column, encoding);
    return value_to_python(unwrap_expected(table.get_value(row, index)), encoding);
}

[[nodiscard]] nb::object table_value_raw(
    const cricodecs::utf::UtfTable& table,
    uint32_t row,
    const nb::object& column
) {
    const auto index = checked_column_key(table, column, nb::none());
    return value_to_python_raw(unwrap_expected(table.get_value(row, index)));
}

[[nodiscard]] nb::object table_default_value(
    const cricodecs::utf::UtfTable& table,
    const nb::object& column,
    const nb::object& encoding
) {
    const auto index = checked_column_key(table, column, encoding);
    return value_to_python(unwrap_expected(table.get_default_value(index)), encoding);
}

[[nodiscard]] nb::object table_default_value_raw(
    const cricodecs::utf::UtfTable& table,
    const nb::object& column
) {
    const auto index = checked_column_key(table, column, nb::none());
    return value_to_python_raw(unwrap_expected(table.get_default_value(index)));
}

[[nodiscard]] std::string table_string(
    const cricodecs::utf::UtfTable& table,
    uint32_t row,
    const nb::object& column,
    const nb::object& encoding
) {
    const auto index = checked_column_key(table, column, encoding);
    return decode_cri_text(unwrap_expected(table.get_string(row, index)), encoding);
}

[[nodiscard]] nb::bytes table_string_raw(
    const cricodecs::utf::UtfTable& table,
    uint32_t row,
    const nb::object& column
) {
    const auto index = checked_column_key(table, column, nb::none());
    return string_view_to_python_bytes(unwrap_expected(table.get_string(row, index)));
}

[[nodiscard]] nb::bytes table_data(
    const cricodecs::utf::UtfTable& table,
    uint32_t row,
    const nb::object& column
) {
    const auto index = checked_column_key(table, column, nb::none());
    return to_python_bytes(unwrap_expected(table.get_data(row, index)));
}

[[nodiscard]] nb::bytes table_default_data(
    const cricodecs::utf::UtfTable& table,
    const nb::object& column
) {
    const auto index = checked_column_key(table, column, nb::none());
    return to_python_bytes(unwrap_expected(table.get_default_data(index)));
}

[[nodiscard]] nb::object table_info(const cricodecs::utf::UtfTable& table, const nb::object& encoding) {
    nb::object info = simple_namespace();
    info.attr("table_name") = decode_cri_text(table.table_name_bytes(), encoding);
    info.attr("row_count") = table.row_count();
    info.attr("column_count") = table.column_count();
    info.attr("version") = table.version();
    info.attr("table_size") = table.table_size();
    info.attr("row_width") = table.row_width();
    info.attr("data_alignment") = table.data_alignment();
    info.attr("is_loaded") = table.is_loaded();
    nb::list columns;
    for (const auto& column : table.columns()) {
        columns.append(column);
    }
    info.attr("columns") = nb::tuple(columns);
    return info;
}

[[nodiscard]] nb::dict table_row(
    const cricodecs::utf::UtfTable& table,
    uint32_t row,
    const nb::object& encoding
) {
    row = checked_row_index(table, row);
    nb::dict result;
    for (uint32_t column = 0; column < table.column_count(); ++column) {
        const auto& entry = table.column(column);
        result[nb::cast(decode_cri_text(entry.name, encoding))] =
            value_to_python(unwrap_expected(table.get_value(row, column)), encoding);
    }
    return result;
}

[[nodiscard]] nb::tuple table_rows(const cricodecs::utf::UtfTable& table, const nb::object& encoding) {
    nb::list rows;
    for (uint32_t row = 0; row < table.row_count(); ++row) {
        rows.append(table_row(table, row, encoding));
    }
    return nb::tuple(rows);
}

[[nodiscard]] nb::dict table_defaults(const cricodecs::utf::UtfTable& table, const nb::object& encoding) {
    nb::dict result;
    for (uint32_t column = 0; column < table.column_count(); ++column) {
        const auto& entry = table.column(column);
        if (!cricodecs::utf::has_flag(entry.flag, cricodecs::utf::ColumnFlag::Default)) {
            continue;
        }
        result[nb::cast(decode_cri_text(entry.name, encoding))] =
            value_to_python(unwrap_expected(table.get_default_value(column)), encoding);
    }
    return result;
}

} // namespace

void bind_utf_module(nb::module_& module) {
    nb::enum_<cricodecs::utf::ColumnFlag>(module, "ColumnFlag")
        .value("NAME", cricodecs::utf::ColumnFlag::Name)
        .value("DEFAULT", cricodecs::utf::ColumnFlag::Default)
        .value("ROW", cricodecs::utf::ColumnFlag::Row);

    nb::enum_<cricodecs::utf::ColumnType>(module, "ColumnType")
        .value("UINT8", cricodecs::utf::ColumnType::UInt8)
        .value("SINT8", cricodecs::utf::ColumnType::SInt8)
        .value("UINT16", cricodecs::utf::ColumnType::UInt16)
        .value("SINT16", cricodecs::utf::ColumnType::SInt16)
        .value("UINT32", cricodecs::utf::ColumnType::UInt32)
        .value("SINT32", cricodecs::utf::ColumnType::SInt32)
        .value("UINT64", cricodecs::utf::ColumnType::UInt64)
        .value("SINT64", cricodecs::utf::ColumnType::SInt64)
        .value("FLOAT", cricodecs::utf::ColumnType::Float)
        .value("DOUBLE", cricodecs::utf::ColumnType::Double)
        .value("STRING", cricodecs::utf::ColumnType::String)
        .value("VLDATA", cricodecs::utf::ColumnType::VLData)
        .value("GUID", cricodecs::utf::ColumnType::GUID);

    nb::class_<cricodecs::utf::DataRef>(module, "DataRef")
        .def(nb::init<>())
        .def_rw("offset", &cricodecs::utf::DataRef::offset)
        .def_rw("size", &cricodecs::utf::DataRef::size);

    nb::class_<cricodecs::utf::GUID>(module, "Guid")
        .def(nb::init<>())
        .def_prop_rw(
            "bytes",
            [](const cricodecs::utf::GUID& guid) {
                return to_python_bytes(std::span<const uint8_t>(guid.data, sizeof(guid.data)));
            },
            [](cricodecs::utf::GUID& guid, const nb::bytes& value) {
                guid = guid_from_python_bytes(value);
            }
        );

    nb::class_<cricodecs::utf::Column>(module, "Column")
        .def_prop_ro("name", [](const cricodecs::utf::Column& column) {
            return decode_cri_text(column.name, nb::none());
        })
        .def_prop_ro("name_raw", [](const cricodecs::utf::Column& column) {
            return string_view_to_python_bytes(column.name);
        })
        .def_ro("type", &cricodecs::utf::Column::type)
        .def_ro("flag", &cricodecs::utf::Column::flag)
        .def_prop_ro("flag_bits", [](const cricodecs::utf::Column& column) {
            return static_cast<uint8_t>(column.flag);
        })
        .def_prop_ro("has_default", [](const cricodecs::utf::Column& column) {
            return cricodecs::utf::has_flag(column.flag, cricodecs::utf::ColumnFlag::Default);
        })
        .def_prop_ro("has_row", [](const cricodecs::utf::Column& column) {
            return cricodecs::utf::has_flag(column.flag, cricodecs::utf::ColumnFlag::Row);
        })
        .def_prop_ro("offset", [](const cricodecs::utf::Column& column) {
            return cricodecs::utf::has_flag(column.flag, cricodecs::utf::ColumnFlag::Row)
                ? column.row_offset
                : column.default_offset;
        })
        .def_ro("default_offset", &cricodecs::utf::Column::default_offset)
        .def_ro("row_offset", &cricodecs::utf::Column::row_offset);

    nb::class_<cricodecs::utf::UtfTable>(module, "Utf")
        .def_static(
            "load",
            &load_utf_any_with_encoding,
            nb::arg("source"),
            nb::arg("encoding") = nb::none(),
            "Load a UTF table from a path, buffer-backed object, or binary file-like object."
        )
        .def_static(
            "load_bytes",
            &load_utf_bytes,
            nb::arg("data"),
            "Load a UTF table from raw bytes, keeping an owned copy alive inside the wrapper."
        )
        .def_static(
            "load_source",
            &load_utf_source,
            nb::arg("source"),
            "Load a UTF table from a buffer-backed Python object."
        )
        .def_static(
            "create",
            [](const nb::object& table_name, uint16_t version, const nb::object& encoding) {
                auto table = create_utf_table(table_name, version, encoding);
                set_text_encoding_from_python(table, encoding);
                return table;
            },
            nb::arg("table_name"),
            nb::arg("version") = static_cast<uint16_t>(0),
            nb::arg("encoding") = nb::none(),
            "Create an empty editable UTF table."
        )
        .def_prop_ro("table_name", [](const cricodecs::utf::UtfTable& self) {
            return decode_cri_text(self.table_name_bytes(), effective_encoding_object(self, nb::none()));
        })
        .def_prop_ro("table_name_raw", [](const cricodecs::utf::UtfTable& self) {
            return string_view_to_python_bytes(self.table_name_bytes());
        })
        .def(
            "table_name_text",
            [](const cricodecs::utf::UtfTable& self, const nb::object& encoding) {
                return decode_cri_text(self.table_name_bytes(), effective_encoding_object(self, encoding));
            },
            nb::arg("encoding") = nb::none()
        )
        .def_prop_ro("row_count", [](const cricodecs::utf::UtfTable& self) {
            return self.row_count();
        })
        .def_prop_ro("column_count", [](const cricodecs::utf::UtfTable& self) {
            return self.column_count();
        })
        .def_prop_ro("version", [](const cricodecs::utf::UtfTable& self) {
            return self.version();
        })
        .def_prop_ro("table_size", [](const cricodecs::utf::UtfTable& self) {
            return self.table_size();
        })
        .def_prop_ro("row_width", [](const cricodecs::utf::UtfTable& self) {
            return self.row_width();
        })
        .def_prop_ro("data_alignment", [](const cricodecs::utf::UtfTable& self) {
            return self.data_alignment();
        })
        .def_prop_ro("is_loaded", [](const cricodecs::utf::UtfTable& self) {
            return self.is_loaded();
        })
        .def_prop_ro("columns", [](const cricodecs::utf::UtfTable& self) {
            nb::list columns;
            for (const auto& column : self.columns()) {
                columns.append(column);
            }
            return columns;
        })
        .def(
            "info",
            [](const cricodecs::utf::UtfTable& self, const nb::object& encoding) {
                return table_info(self, effective_encoding_object(self, encoding));
            },
            nb::arg("encoding") = nb::none()
        )
        .def(
            "column",
            [](const cricodecs::utf::UtfTable& self, uint32_t index) {
                return checked_column_at(self, index);
            },
            nb::arg("index")
        )
        .def(
            "find_column",
            [](const cricodecs::utf::UtfTable& self, const nb::object& name, const nb::object& encoding) {
                const auto effective_encoding = effective_encoding_object(self, encoding);
                const auto raw_name = raw_cri_string_from_python(name, effective_encoding);
                return self.find_column(raw_name);
            },
            nb::arg("name"),
            nb::arg("encoding") = nb::none()
        )
        .def(
            "column_name",
            [](const cricodecs::utf::UtfTable& self, uint32_t index, const nb::object& encoding) {
                const auto effective_encoding = effective_encoding_object(self, encoding);
                const auto& column = checked_column_at(self, index);
                return decode_cri_text(column.name, effective_encoding);
            },
            nb::arg("index"),
            nb::arg("encoding") = nb::none()
        )
        .def(
            "column_name_raw",
            [](const cricodecs::utf::UtfTable& self, uint32_t index) {
                return string_view_to_python_bytes(checked_column_at(self, index).name);
            },
            nb::arg("index")
        )
        .def(
            "value",
            [](const cricodecs::utf::UtfTable& self, uint32_t row, const nb::object& column, const nb::object& encoding) {
                return table_value(self, row, column, effective_encoding_object(self, encoding));
            },
            nb::arg("row"),
            nb::arg("column"),
            nb::arg("encoding") = nb::none()
        )
        .def("value_raw", &table_value_raw, nb::arg("row"), nb::arg("column"))
        .def(
            "default_value",
            [](const cricodecs::utf::UtfTable& self, const nb::object& column, const nb::object& encoding) {
                return table_default_value(self, column, effective_encoding_object(self, encoding));
            },
            nb::arg("column"),
            nb::arg("encoding") = nb::none()
        )
        .def("default_value_raw", &table_default_value_raw, nb::arg("column"))
        .def("default_data", &table_default_data, nb::arg("column"))
        .def(
            "string",
            [](const cricodecs::utf::UtfTable& self, uint32_t row, const nb::object& column, const nb::object& encoding) {
                return table_string(self, row, column, effective_encoding_object(self, encoding));
            },
            nb::arg("row"),
            nb::arg("column"),
            nb::arg("encoding") = nb::none()
        )
        .def("string_raw", &table_string_raw, nb::arg("row"), nb::arg("column"))
        .def("data", &table_data, nb::arg("row"), nb::arg("column"))
        .def(
            "row",
            [](const cricodecs::utf::UtfTable& self, uint32_t row, const nb::object& encoding) {
                return table_row(self, row, effective_encoding_object(self, encoding));
            },
            nb::arg("row"),
            nb::arg("encoding") = nb::none()
        )
        .def(
            "rows",
            [](const cricodecs::utf::UtfTable& self, const nb::object& encoding) {
                return table_rows(self, effective_encoding_object(self, encoding));
            },
            nb::arg("encoding") = nb::none()
        )
        .def(
            "defaults",
            [](const cricodecs::utf::UtfTable& self, const nb::object& encoding) {
                return table_defaults(self, effective_encoding_object(self, encoding));
            },
            nb::arg("encoding") = nb::none()
        )
        .def(
            "add_column",
            [](cricodecs::utf::UtfTable& self, const nb::object& name, cricodecs::utf::ColumnType type, const nb::object& flag, const nb::object& encoding) {
                ensure_editable(self);
                const auto effective_encoding = effective_encoding_object(self, encoding);
                const auto raw_name = raw_cri_string_from_python(name, effective_encoding);
                const uint8_t flag_bits = column_flag_bits_from_python(flag);
                self.add_column(raw_name, type, static_cast<cricodecs::utf::ColumnFlag>(flag_bits));
            },
            nb::arg("name"),
            nb::arg("type"),
            nb::arg("flag") = nb::cast(cricodecs::utf::ColumnFlag::Name),
            nb::arg("encoding") = nb::none()
        )
        .def(
            "add_row",
            [](cricodecs::utf::UtfTable& self) {
                ensure_editable(self);
                return self.add_row();
            }
        )
        .def(
            "set",
            [](cricodecs::utf::UtfTable& self, uint32_t row, const nb::object& column, const nb::object& value, const nb::object& encoding) {
                ensure_editable(self);
                const auto effective_encoding = effective_encoding_object(self, encoding);
                const auto row_index = checked_row_index(self, row);
                const auto column_index = checked_column_key(self, column, effective_encoding);
                const auto column_type = self.column(column_index).type;
                self.set(row_index, column_index, python_to_utf_value(value, column_type, effective_encoding));
            },
            nb::arg("row"),
            nb::arg("column"),
            nb::arg("value"),
            nb::arg("encoding") = nb::none()
        )
        .def(
            "set_default_value",
            [](cricodecs::utf::UtfTable& self, const nb::object& column, const nb::object& value, const nb::object& encoding) {
                ensure_editable(self);
                const auto effective_encoding = effective_encoding_object(self, encoding);
                const auto column_index = checked_column_key(self, column, effective_encoding);
                const auto column_type = self.column(column_index).type;
                self.set_default_value(column_index, python_to_utf_value(value, column_type, effective_encoding));
            },
            nb::arg("column"),
            nb::arg("value"),
            nb::arg("encoding") = nb::none()
        )
        .def(
            "set_row_width",
            [](cricodecs::utf::UtfTable& self, uint16_t row_width) {
                ensure_editable(self);
                self.set_row_width(row_width);
            },
            nb::arg("row_width")
        )
        .def(
            "set_data_alignment",
            [](cricodecs::utf::UtfTable& self, uint32_t alignment) {
                ensure_editable(self);
                self.set_data_alignment(alignment);
            },
            nb::arg("alignment")
        )
        .def(
            "build",
            [](cricodecs::utf::UtfTable& self) {
                ensure_editable(self);
                return to_python_bytes(self.build());
            }
        );

    install_attr_repr(module, "DataRef", {"offset", "size"});
    install_attr_repr(module, "Guid", {"bytes"});
    install_attr_repr(module, "Column", {"name", "type", "flag", "flag_bits", "has_default", "has_row", "offset", "default_offset", "row_offset"});
    install_attr_repr(module, "Utf", {"table_name", "row_count", "column_count", "version", "table_size", "row_width", "data_alignment", "is_loaded", "columns"});

    module.def("load", &load_utf_any_with_encoding, nb::arg("source"), nb::arg("encoding") = nb::none());
    module.def(
        "create",
        [](const nb::object& table_name, uint16_t version, const nb::object& encoding) {
            auto table = create_utf_table(table_name, version, encoding);
            set_text_encoding_from_python(table, encoding);
            return table;
        },
        nb::arg("table_name"),
        nb::arg("version") = static_cast<uint16_t>(0),
        nb::arg("encoding") = nb::none()
    );
}

} // namespace cricodecs::python

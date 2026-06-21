#pragma once

#include <cstdint>
#include <expected>
#include <cstring>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <memory>
#include <optional>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include "../../CriCodecs/src/utilities/text_encoding.hpp"
#include "../../CriCodecs/src/wav/wav_container.hpp"

namespace nb = nanobind;

namespace cricodecs::python {

void bind_adx_module(nb::module_& module);
void bind_ahx_module(nb::module_& module);
void bind_aax_module(nb::module_& module);
void bind_aix_module(nb::module_& module);
void bind_acx_module(nb::module_& module);
void bind_acb_module(nb::module_& module);
void bind_hca_module(nb::module_& module);
void bind_awb_module(nb::module_& module);
void bind_afs_module(nb::module_& module);
void bind_cpk_module(nb::module_& module);
void bind_csb_module(nb::module_& module);
void bind_cvm_module(nb::module_& module);
void bind_sfd_module(nb::module_& module);
void bind_usm_module(nb::module_& module);
void bind_utf_module(nb::module_& module);
void bind_video_module(nb::module_& module);
void bind_wav_module(nb::module_& module);

[[noreturn]] inline void raise_value_error(std::string_view message) {
    const std::string owned(message);
    PyErr_SetString(PyExc_ValueError, owned.c_str());
    throw nb::python_error();
}

[[noreturn]] inline void raise_type_error(std::string_view message) {
    const std::string owned(message);
    PyErr_SetString(PyExc_TypeError, owned.c_str());
    throw nb::python_error();
}

template <typename T>
[[nodiscard]] inline T unwrap_expected(std::expected<T, std::string>&& result) {
    if (!result) {
        raise_value_error(result.error());
    }
    return std::move(result).value();
}

class BorrowedPythonBytes {
public:
    BorrowedPythonBytes() noexcept = default;
    BorrowedPythonBytes(const BorrowedPythonBytes&) = delete;
    BorrowedPythonBytes& operator=(const BorrowedPythonBytes&) = delete;

    BorrowedPythonBytes(BorrowedPythonBytes&& other) noexcept {
        *this = std::move(other);
    }

    BorrowedPythonBytes& operator=(BorrowedPythonBytes&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        release();
        buffer_ = other.buffer_;
        source_ = std::move(other.source_);
        acquired_ = other.acquired_;
        other.acquired_ = false;
        return *this;
    }

    explicit BorrowedPythonBytes(const nb::handle& object) {
        if (!try_acquire(object)) {
            raise_type_error("source must be a contiguous, byte-sized buffer object");
        }
    }

    ~BorrowedPythonBytes() {
        release();
    }

    [[nodiscard]] std::span<const uint8_t> view() const {
        if (!acquired_) {
            return {};
        }
        return {
            static_cast<const uint8_t*>(buffer_.buf),
            static_cast<size_t>(buffer_.len),
        };
    }

    [[nodiscard]] bool try_acquire(const nb::handle& object) {
        release();
        if (PyObject_GetBuffer(object.ptr(), &buffer_, PyBUF_CONTIG_RO) != 0) {
            PyErr_Clear();
            return false;
        }

        acquired_ = true;
        source_ = nb::borrow<nb::object>(object);
        if (buffer_.itemsize != 1) {
            release();
            raise_type_error("buffer object is not byte-sized; expected itemsize 1");
        }

        if (buffer_.ndim != 1) {
            release();
            raise_type_error("buffer object must be one-dimensional contiguous bytes");
        }

        if (buffer_.strides != nullptr && buffer_.strides[0] != static_cast<Py_ssize_t>(buffer_.itemsize)) {
            release();
            raise_type_error("buffer object must be contiguous in memory");
        }

        if (buffer_.suboffsets != nullptr) {
            release();
            raise_type_error("buffer object uses suboffsets and is not supported");
        }

        return true;
    }

private:
    void release() noexcept {
        if (acquired_) {
            PyBuffer_Release(&buffer_);
        }
        acquired_ = false;
        std::memset(&buffer_, 0, sizeof(buffer_));
        source_.reset();
    }

    Py_buffer buffer_{};
    bool acquired_ = false;
    nb::object source_;

public:
    [[nodiscard]] std::span<const uint8_t> as_span() const noexcept { return view(); }
    [[nodiscard]] const uint8_t* data() const noexcept { return static_cast<const uint8_t*>(buffer_.buf); }
    [[nodiscard]] size_t size() const noexcept { return static_cast<size_t>(buffer_.len); }
};

[[nodiscard]] inline std::shared_ptr<const void> keep_python_bytes_alive(BorrowedPythonBytes&& borrowed) {
    return std::static_pointer_cast<const void>(
        std::make_shared<BorrowedPythonBytes>(std::move(borrowed))
    );
}

[[nodiscard]] inline nb::object call_noarg_method(const nb::handle& object, const char* name) {
    return nb::borrow<nb::object>(object).attr(name)();
}

[[nodiscard]] inline nb::object simple_namespace() {
    static nb::object type = nb::module_::import_("types").attr("SimpleNamespace");
    return type();
}

[[nodiscard]] inline std::string python_repr_string(PyObject* object) {
    PyObject* repr = PyObject_Repr(object);
    if (repr == nullptr) {
        throw nb::python_error();
    }
    auto repr_object = nb::steal<nb::object>(repr);
    return nb::cast<std::string>(repr_object);
}

inline void install_attr_repr(
    nb::module_& module,
    std::string_view class_name,
    std::initializer_list<const char*> attrs)
{
    const auto module_name = nb::cast<std::string>(module.attr("__name__"));
    std::vector<std::string> names;
    names.reserve(attrs.size());
    for (const char* attr : attrs) {
        names.emplace_back(attr);
    }

    const auto type_name = std::format("{}.{}", module_name, class_name);
    module.attr(class_name.data()).attr("__repr__") = nb::cpp_function(
        [type_name, names](nb::handle self) {
            std::string result = std::format("{}(", type_name);
            bool first = true;
            for (const auto& name : names) {
                PyObject* value = PyObject_GetAttrString(self.ptr(), name.c_str());
                if (value == nullptr) {
                    PyErr_Clear();
                    continue;
                }
                auto value_object = nb::steal<nb::object>(value);
                if (PyCallable_Check(value_object.ptr()) != 0) {
                    continue;
                }

                result += std::format("{}{}={}", first ? "" : ", ", name, python_repr_string(value_object.ptr()));
                first = false;
            }
            result += ')';
            return result;
        },
        nb::is_method()
    );
}

[[nodiscard]] inline bool has_callable_attr(const nb::handle& object, const char* name) {
    PyObject* attr = PyObject_GetAttrString(object.ptr(), name);
    if (attr == nullptr) {
        PyErr_Clear();
        return false;
    }
    const bool callable = PyCallable_Check(attr) != 0;
    Py_DECREF(attr);
    return callable;
}

[[nodiscard]] inline BorrowedPythonBytes borrow_python_source(const nb::handle& source) {
    BorrowedPythonBytes borrowed;
    if (borrowed.try_acquire(source)) {
        return borrowed;
    }

    if (has_callable_attr(source, "getbuffer")) {
        auto buffer = call_noarg_method(source, "getbuffer");
        if (borrowed.try_acquire(buffer)) {
            return borrowed;
        }
    }

    if (has_callable_attr(source, "read")) {
        auto data = call_noarg_method(source, "read");
        if (PyUnicode_Check(data.ptr())) {
            raise_type_error("source must be a binary file-like object, got a text stream");
        }
        if (borrowed.try_acquire(data)) {
            return borrowed;
        }
        raise_type_error("source.read() must return a contiguous, byte-sized buffer object");
    }

    raise_type_error("source must be a contiguous byte buffer, object with getbuffer(), or binary file-like object");
}

[[nodiscard]] inline std::optional<std::string> python_text_path(const nb::handle& source) {
    PyObject* fs_path = PyOS_FSPath(source.ptr());
    if (fs_path == nullptr) {
        PyErr_Clear();
        return std::nullopt;
    }

    auto path = nb::steal<nb::object>(fs_path);
    if (!PyUnicode_Check(path.ptr())) {
        return std::nullopt;
    }
    return nb::cast<std::string>(path);
}

[[nodiscard]] inline std::filesystem::path require_python_path(
    const nb::handle& source,
    std::string_view argument_name)
{
    if (auto path = python_text_path(source)) {
        return std::filesystem::path(*path);
    }
    raise_type_error(std::string(argument_name) + " must be str or os.PathLike");
}

template <typename Loader>
[[nodiscard]] inline auto load_path_or_borrowed_source(const nb::object& source, Loader&& loader) {
    if (auto path = python_text_path(source)) {
        return loader(std::filesystem::path(*path), std::span<const uint8_t>{});
    }
    auto borrowed = borrow_python_source(source);
    return loader(std::filesystem::path{}, borrowed.as_span());
}

[[nodiscard]] inline std::string_view borrow_python_bytes(const nb::bytes& bytes) {
    char* data = nullptr;
    Py_ssize_t size = 0;
    if (PyBytes_AsStringAndSize(bytes.ptr(), &data, &size) != 0) {
        throw nb::python_error();
    }
    return {data, static_cast<size_t>(size)};
}

[[nodiscard]] inline std::span<const uint8_t> as_byte_span(std::string_view view) noexcept {
    return {
        reinterpret_cast<const uint8_t*>(view.data()),
        view.size(),
    };
}

[[nodiscard]] inline std::vector<uint8_t> copy_python_bytes(const nb::bytes& bytes) {
    const auto view = borrow_python_bytes(bytes);
    if (view.empty()) {
        return {};
    }
    return {
        reinterpret_cast<const uint8_t*>(view.data()),
        reinterpret_cast<const uint8_t*>(view.data()) + view.size(),
    };
}

[[nodiscard]] inline nb::bytes to_python_bytes(std::span<const uint8_t> bytes) {
    PyObject* object = PyBytes_FromStringAndSize(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<Py_ssize_t>(bytes.size())
    );
    if (object == nullptr) {
        throw nb::python_error();
    }
    return nb::steal<nb::bytes>(object);
}

[[nodiscard]] inline cricodecs::text::EncodingOptions python_encoding_options_from_object(const nb::object& encoding) {
    cricodecs::text::EncodingOptions options;
    if (!encoding.is_none()) {
        options.encoding = nb::cast<std::string>(encoding);
    }
    return options;
}

[[nodiscard]] inline nb::bytes string_to_python_bytes(std::string_view bytes) {
    return to_python_bytes(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(bytes.data()),
        bytes.size()
    ));
}

[[nodiscard]] inline nb::bytes pcm16_to_python_bytes(std::span<const int16_t> samples) {
    PyObject* object = PyBytes_FromStringAndSize(nullptr, static_cast<Py_ssize_t>(samples.size() * sizeof(int16_t)));
    if (object == nullptr) {
        throw nb::python_error();
    }
    auto bytes = nb::steal<nb::bytes>(object);
    auto* data = reinterpret_cast<uint8_t*>(PyBytes_AsString(bytes.ptr()));
    if (data == nullptr) {
        throw nb::python_error();
    }
    for (size_t index = 0; index < samples.size(); ++index) {
        const uint16_t raw = static_cast<uint16_t>(samples[index]);
        data[(index * 2) + 0] = static_cast<uint8_t>(raw & 0xFFu);
        data[(index * 2) + 1] = static_cast<uint8_t>((raw >> 8u) & 0xFFu);
    }
    return bytes;
}

[[nodiscard]] inline nb::bytes pcm16_to_wav_python_bytes(
    std::span<const int16_t> samples,
    uint32_t sample_rate,
    uint16_t channels,
    std::span<const cricodecs::wav::SampleLoop> loops = {})
{
    auto wav_bytes = unwrap_expected(cricodecs::wav::WavContainer::build_bytes(
        samples,
        sample_rate,
        channels,
        loops
    ));
    return to_python_bytes(wav_bytes);
}

struct WavPcmInput {
    std::vector<int16_t> samples;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    std::vector<cricodecs::wav::SampleLoop> loops;
};

[[nodiscard]] inline WavPcmInput wav_pcm16_from_python_bytes(
    const nb::bytes& bytes,
    std::string_view context
) {
    const auto view = borrow_python_bytes(bytes);
    cricodecs::wav::WavContainer wav;
    auto loaded = wav.load(as_byte_span(view));
    if (!loaded) {
        raise_value_error(std::string(context) + " encode failed: " + loaded.error());
    }

    auto pcm = wav.get_pcm16();
    if (!pcm) {
        raise_value_error(std::string(context) + " encode failed: " + pcm.error());
    }

    WavPcmInput input;
    input.samples.assign(pcm->begin(), pcm->end());
    input.sample_rate = wav.sample_rate();
    input.channels = static_cast<uint16_t>(wav.channels());
    input.loops = wav.sampler().loops;
    return input;
}

} // namespace cricodecs::python

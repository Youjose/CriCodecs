#include "binding_helpers.hpp"

#include "../../CriCodecs/src/cli/cli.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <nanobind/stl/vector.h>

namespace cricodecs::python {
namespace {

constexpr const char* kModuleDoc =
    "Thin nanobind bindings for the current CriCodecs ADX, AHX, AAX, AIX, ACX, ACB, HCA, AWB, AFS, CPK, CSB, CVM, SFD, USM, UTF, video, and WAV public APIs.";

constexpr size_t kSniffSize = 64;
constexpr uintmax_t kUnknownPathProbeLimit = 16u * 1024u * 1024u;

struct Candidate {
    std::string_view name;
    nb::object module;
};

struct PreparedLoadSource {
    nb::object object;
    std::array<uint8_t, kSniffSize> prefix{};
    size_t prefix_size = 0;
    bool is_path = false;
    uintmax_t path_size = 0;
};

[[nodiscard]] PreparedLoadSource prepare_load_source(const nb::handle& source) {
    PreparedLoadSource prepared;

    if (auto path = python_text_path(source)) {
        prepared.is_path = true;
        prepared.object = nb::borrow<nb::object>(source);
        std::ifstream input(*path, std::ios::binary);
        if (!input) {
            raise_value_error("could not detect supported format from unreadable file path");
        }
        input.read(
            reinterpret_cast<char*>(prepared.prefix.data()),
            static_cast<std::streamsize>(prepared.prefix.size())
        );
        prepared.prefix_size = static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
        std::error_code error;
        prepared.path_size = std::filesystem::file_size(std::filesystem::path(*path), error);
        if (error) {
            prepared.path_size = kUnknownPathProbeLimit + 1;
        }
        return prepared;
    }

    BorrowedPythonBytes borrowed;
    if (borrowed.try_acquire(source)) {
        const auto view = borrowed.as_span();
        prepared.object = nb::borrow<nb::object>(source);
        prepared.prefix_size = std::min(view.size(), prepared.prefix.size());
        std::ranges::copy(view.first(prepared.prefix_size), prepared.prefix.begin());
        return prepared;
    }

    if (has_callable_attr(source, "getbuffer")) {
        auto buffer = call_noarg_method(source, "getbuffer");
        if (borrowed.try_acquire(buffer)) {
            const auto view = borrowed.as_span();
            prepared.object = std::move(buffer);
            prepared.prefix_size = std::min(view.size(), prepared.prefix.size());
            std::ranges::copy(view.first(prepared.prefix_size), prepared.prefix.begin());
            return prepared;
        }
    }

    if (has_callable_attr(source, "read")) {
        auto data = call_noarg_method(source, "read");
        if (PyUnicode_Check(data.ptr())) {
            raise_type_error("source must be a binary file-like object, got a text stream");
        }
        if (!borrowed.try_acquire(data)) {
            raise_type_error("source.read() must return a contiguous, byte-sized buffer object");
        }
        const auto view = borrowed.as_span();
        prepared.object = std::move(data);
        prepared.prefix_size = std::min(view.size(), prepared.prefix.size());
        std::ranges::copy(view.first(prepared.prefix_size), prepared.prefix.begin());
        return prepared;
    }

    raise_type_error("source must be a path, contiguous byte buffer, object with getbuffer(), or binary file-like object");
}

[[nodiscard]] std::vector<Candidate> load_order(
    const nb::module_& wav,
    const nb::module_& hca,
    const nb::module_& adx,
    const nb::module_& aix,
    const nb::module_& aax,
    const nb::module_& acb,
    const nb::module_& utf,
    const nb::module_& afs,
    const nb::module_& awb,
    const nb::module_& cpk,
    const nb::module_& acx,
    const nb::module_& sfd,
    const nb::module_& usm,
    const nb::module_& video,
    const nb::module_& csb,
    const nb::module_& cvm)
{
    return {
        {"wav", nb::borrow<nb::object>(wav)},
        {"hca", nb::borrow<nb::object>(hca)},
        {"adx", nb::borrow<nb::object>(adx)},
        {"aix", nb::borrow<nb::object>(aix)},
        {"aax", nb::borrow<nb::object>(aax)},
        {"acb", nb::borrow<nb::object>(acb)},
        {"utf", nb::borrow<nb::object>(utf)},
        {"afs", nb::borrow<nb::object>(afs)},
        {"awb", nb::borrow<nb::object>(awb)},
        {"cpk", nb::borrow<nb::object>(cpk)},
        {"acx", nb::borrow<nb::object>(acx)},
        {"sfd", nb::borrow<nb::object>(sfd)},
        {"usm", nb::borrow<nb::object>(usm)},
        {"video", nb::borrow<nb::object>(video)},
        {"csb", nb::borrow<nb::object>(csb)},
        {"cvm", nb::borrow<nb::object>(cvm)},
    };
}

[[nodiscard]] std::vector<Candidate> candidate_modules(
    std::span<const uint8_t> prefix,
    const std::vector<Candidate>& order)
{
    const auto find = [&](std::string_view name) -> Candidate {
        const auto it = std::ranges::find(order, name, &Candidate::name);
        return it == order.end() ? Candidate{} : *it;
    };

    std::vector<Candidate> candidates;
    for (const auto format : cricodecs::cli::sniff_format_order(prefix, true)) {
        const auto candidate = find(cricodecs::cli::format_key(format));
        if (!candidate.name.empty()) {
            candidates.push_back(candidate);
        }
    }
    return candidates;
}

[[nodiscard]] nb::object loaded_types_tuple(
    const nb::module_& wav,
    const nb::module_& hca,
    const nb::module_& adx,
    const nb::module_& aix,
    const nb::module_& aax,
    const nb::module_& acb,
    const nb::module_& utf,
    const nb::module_& afs,
    const nb::module_& awb,
    const nb::module_& cpk,
    const nb::module_& acx,
    const nb::module_& sfd,
    const nb::module_& usm,
    const nb::module_& csb,
    const nb::module_& cvm,
    const nb::module_& video)
{
    return nb::make_tuple(
        wav.attr("Wav"),
        hca.attr("Hca"),
        adx.attr("Adx"),
        aix.attr("Aix"),
        aax.attr("Aax"),
        acb.attr("Acb"),
        utf.attr("Utf"),
        afs.attr("Afs"),
        awb.attr("Awb"),
        cpk.attr("Cpk"),
        acx.attr("Acx"),
        sfd.attr("Sfd"),
        usm.attr("Usm"),
        csb.attr("Csb"),
        cvm.attr("Cvm"),
        video.attr("Ivf"),
        video.attr("Mpeg"),
        video.attr("H264")
    );
}

[[nodiscard]] nb::object load_detected_object(
    const nb::handle& source,
    const nb::object& loaded_types,
    const std::vector<Candidate>& order)
{
    const int already_loaded = PyObject_IsInstance(source.ptr(), loaded_types.ptr());
    if (already_loaded < 0) {
        throw nb::python_error();
    }
    if (already_loaded != 0) {
        return nb::borrow<nb::object>(source);
    }

    auto prepared = prepare_load_source(source);
    const auto prefix = std::span<const uint8_t>(prepared.prefix.data(), prepared.prefix_size);
    auto candidates = candidate_modules(prefix, order);
    if (candidates.empty()) {
        if (prepared.is_path && prepared.path_size > kUnknownPathProbeLimit) {
            raise_value_error("could not detect supported format from file header");
        }
        candidates = order;
    }

    struct Failure {
        std::string name;
        std::string message;
        int score = 0;
    };
    std::vector<Failure> failures;
    failures.reserve(candidates.size());

    for (const auto& candidate : candidates) {
        try {
            return nb::cast<nb::object>(candidate.module.attr("load")(prepared.object));
        } catch (const nb::python_error& error) {
            PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
            PyErr_Fetch(&type, &value, &traceback);
            nb::object value_object;
            std::string message;
            if (value != nullptr) {
                value_object = nb::steal<nb::object>(value);
                message = nb::cast<std::string>(nb::str(value_object));
            } else {
                message = error.what();
            }
            Py_XDECREF(type);
            Py_XDECREF(traceback);
            PyErr_Clear();
            failures.push_back(Failure{
                std::string(candidate.name),
                message,
                cricodecs::cli::error_suspicion(message),
            });
        }
    }

    if (failures.empty()) {
        raise_value_error("could not detect supported format");
    }

    const auto best = std::ranges::max_element(failures, {}, &Failure::score);
    std::string message = "could not detect supported format";
    if (best != failures.end() && best->score >= 2) {
        message += "; most suspicious failure from ";
        message += best->name;
        message += ": ";
        message += best->message;
    }
    raise_value_error(message);
}

[[nodiscard]] std::vector<std::string> python_cli_args() {
    PyObject* argv = PySys_GetObject("argv");
    if (argv == nullptr) {
        return {};
    }

    const Py_ssize_t count = PySequence_Size(argv);
    if (count < 0) {
        throw nb::python_error();
    }

    std::vector<std::string> args;
    args.reserve(count > 0 ? static_cast<size_t>(count - 1) : 0u);
    for (Py_ssize_t index = 1; index < count; ++index) {
        nb::object item = nb::steal<nb::object>(PySequence_GetItem(argv, index));
        if (!item.is_valid()) {
            throw nb::python_error();
        }
        if (!PyUnicode_Check(item.ptr())) {
            raise_type_error("sys.argv entries must be strings");
        }
        Py_ssize_t size = 0;
        const char* text = PyUnicode_AsUTF8AndSize(item.ptr(), &size);
        if (text == nullptr) {
            throw nb::python_error();
        }
        args.emplace_back(text, static_cast<size_t>(size));
    }
    return args;
}

} // namespace
} // namespace cricodecs::python

NB_MODULE(cricodecs, module) {
    module.doc() = cricodecs::python::kModuleDoc;

    auto wav_module = module.def_submodule("wav", "WAV container helpers");
    cricodecs::python::bind_wav_module(wav_module);

    auto adx_module = module.def_submodule("adx", "ADX codec helpers");
    cricodecs::python::bind_adx_module(adx_module);

    auto ahx_module = module.def_submodule("ahx", "AHX codec helpers");
    cricodecs::python::bind_ahx_module(ahx_module);

    auto aax_module = module.def_submodule("aax", "AAX wrapper helpers");
    cricodecs::python::bind_aax_module(aax_module);

    auto aix_module = module.def_submodule("aix", "AIX container helpers");
    cricodecs::python::bind_aix_module(aix_module);

    auto acx_module = module.def_submodule("acx", "ACX archive helpers");
    cricodecs::python::bind_acx_module(acx_module);

    auto acb_module = module.def_submodule("acb", "ACB waveform helpers");
    cricodecs::python::bind_acb_module(acb_module);

    auto hca_module = module.def_submodule("hca", "HCA codec helpers");
    cricodecs::python::bind_hca_module(hca_module);

    auto awb_module = module.def_submodule("awb", "AWB container helpers");
    cricodecs::python::bind_awb_module(awb_module);

    auto afs_module = module.def_submodule("afs", "AFS container helpers");
    cricodecs::python::bind_afs_module(afs_module);

    auto cpk_module = module.def_submodule("cpk", "CPK archive helpers");
    cricodecs::python::bind_cpk_module(cpk_module);

    auto csb_module = module.def_submodule("csb", "CSB cue archive helpers");
    cricodecs::python::bind_csb_module(csb_module);

    auto cvm_module = module.def_submodule("cvm", "CVM image helpers");
    cricodecs::python::bind_cvm_module(cvm_module);

    auto sfd_module = module.def_submodule("sfd", "SFD container helpers");
    cricodecs::python::bind_sfd_module(sfd_module);

    auto usm_module = module.def_submodule("usm", "USM container helpers");
    cricodecs::python::bind_usm_module(usm_module);

    auto utf_module = module.def_submodule("utf", "UTF table helpers");
    cricodecs::python::bind_utf_module(utf_module);

    auto video_module = module.def_submodule("video", "Video elementary stream readers");
    cricodecs::python::bind_video_module(video_module);

    auto order = cricodecs::python::load_order(
        wav_module,
        hca_module,
        adx_module,
        aix_module,
        aax_module,
        acb_module,
        utf_module,
        afs_module,
        awb_module,
        cpk_module,
        acx_module,
        sfd_module,
        usm_module,
        video_module,
        csb_module,
        cvm_module
    );
    auto loaded_types = cricodecs::python::loaded_types_tuple(
        wav_module,
        hca_module,
        adx_module,
        aix_module,
        aax_module,
        acb_module,
        utf_module,
        afs_module,
        awb_module,
        cpk_module,
        acx_module,
        sfd_module,
        usm_module,
        csb_module,
        cvm_module,
        video_module
    );

    module.def(
        "load",
        [loaded_types = std::move(loaded_types), order = std::move(order)](const nb::object& source) {
            return cricodecs::python::load_detected_object(source, loaded_types, order);
        },
        nb::arg("source"),
        "Detect a supported CRI format and return the matching loaded object."
    );

    module.def(
        "_error_suspicion",
        [](const std::string& message) {
            return cricodecs::cli::error_suspicion(message);
        },
        nb::arg("message")
    );

    module.def(
        "_run_cli",
        [](const std::vector<std::string>& argv) {
            return cricodecs::cli::run(argv, std::cout, std::cerr);
        },
        nb::arg("argv")
    );

    module.def(
        "_main",
        [] {
            return cricodecs::cli::run(cricodecs::python::python_cli_args(), std::cout, std::cerr);
        }
    );
}

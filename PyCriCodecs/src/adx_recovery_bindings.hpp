#pragma once

#include "binding_helpers.hpp"

#include "../../CriCodecs/src/adx/adx_recovery_source_collector.hpp"

namespace cricodecs::python {

[[nodiscard]] inline std::vector<std::vector<uint8_t>> collect_python_adx_family_sources(
    const nb::object& source,
    cricodecs::adx::RecoveryStreamKind kind,
    std::string_view context
) {
    std::vector<nb::object> objects;
    if (PyList_Check(source.ptr()) || PyTuple_Check(source.ptr())) {
        const auto sequence = nb::borrow<nb::sequence>(source);
        objects.reserve(nb::len(sequence));
        for (const auto item : sequence) {
            objects.push_back(nb::borrow<nb::object>(item));
        }
    } else {
        objects.push_back(source);
    }
    if (objects.empty()) {
        raise_value_error(std::string(context) + " requires at least one source");
    }

    std::vector<std::vector<uint8_t>> sources;
    for (const auto& object : objects) {
        std::expected<std::vector<std::vector<uint8_t>>, std::string> collected =
            std::unexpected("uninitialized recovery source");
        if (auto path = python_text_path(object)) {
            collected = cricodecs::adx::collect_recovery_streams(
                std::filesystem::path(*path), kind);
        } else {
            auto borrowed = borrow_python_source(object);
            collected = cricodecs::adx::collect_recovery_streams(
                borrowed.as_span(), kind);
        }
        if (!collected) {
            raise_value_error(std::string(context) + " failed: " + collected.error());
        }
        sources.insert(
            sources.end(),
            std::make_move_iterator(collected->begin()),
            std::make_move_iterator(collected->end()));
    }
    if (sources.empty()) {
        raise_value_error(std::string(context) + " sources contain no encrypted streams");
    }
    return sources;
}

} // namespace cricodecs::python

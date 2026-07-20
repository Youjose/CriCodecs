#include "shared/document_extraction_report.hpp"

#include <utility>

namespace cristudio {

void add_report_success(
    ExtractionReport& report,
    const std::filesystem::path& output_path,
    std::string_view label,
    const ExtractionOptions& options
) {
    ++report.extracted;
    report.output_paths.push_back(output_path);
    auto message = "Extracted " + std::string(label) + " -> " + output_path.string();
    report.messages.push_back(message);
    if (options.event_callback) {
        options.event_callback(ExtractionEvent{
            .extracted_delta = 1,
            .message = std::move(message),
        });
    }
}

void add_report_failure(
    ExtractionReport& report,
    std::string_view label,
    std::string_view error,
    const ExtractionOptions& options
) {
    ++report.failed;
    auto message = "Failed " + std::string(label) + ": " + std::string(error);
    report.messages.push_back(message);
    if (options.event_callback) {
        options.event_callback(ExtractionEvent{
            .failed_delta = 1,
            .message = std::move(message),
        });
    }
}

void add_report_message(
    ExtractionReport& report,
    std::string message,
    const ExtractionOptions& options
) {
    report.messages.push_back(message);
    if (options.event_callback) {
        options.event_callback(ExtractionEvent{.message = std::move(message)});
    }
}

} // namespace cristudio

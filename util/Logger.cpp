#include "Logger.h"

#include "OptionsDB.h"
#include "i18n.h"
#include "Version.h"

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/formatter_parser.hpp>
#include <boost/log/utility/setup/filter_parser.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/expressions/keyword.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/utility/setup/formatter_parser.hpp>
#include <boost/log/utility/setup/filter_parser.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>

#include <ctime>

namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace attr = boost::log::attributes;
namespace keywords = boost::log::keywords;
namespace sinks = boost::log::sinks;


namespace {

    // internal_logger is intentionally omitted from all converters.  It is only used internally.
    // Hence the name.
#define InternalLogger(name) FO_LOGGER(name, LogLevel::internal_logger)

    // Compile time constant pointers to constant char arrays.
    constexpr const char* const log_level_names[] = {"debug", "info ", "warn ", "error", " log "};

    constexpr LogLevel default_sink_level = LogLevel::debug;
    constexpr LogLevel default_source_level = LogLevel::info;

    DiscreteValidator<std::string> LogLevelValidator() {
        std::set<std::string> valid_levels = {"debug", "info", "warn", "error",
                                              "DEBUG", "INFO", "WARN", "ERROR",
                                              "0", "1", "2", "3" };
        auto validator = DiscreteValidator<std::string>(valid_levels);
        return validator;
    }

}

std::string to_string(const LogLevel level)
{ return log_level_names[static_cast<std::size_t>(level)]; }

LogLevel to_LogLevel(const std::string& text) {
    if (text == "error")    return LogLevel::error;
    if (text == "warn")     return LogLevel::warn;
    if (text == "info")     return LogLevel::info;
    return LogLevel::debug;
}

// Provide a LogLevel stream out formatter for streaming logs
template<typename CharT, typename TraitsT>
std::basic_ostream<CharT, TraitsT>& operator<<(
    std::basic_ostream<CharT, TraitsT>& os, const LogLevel& level)
{
    os << log_level_names[static_cast<std::size_t>(level)];
    return os;
}

// Provide a LogLevel input formatter for filtering
template<typename CharT, typename TraitsT>
inline std::basic_istream<CharT, TraitsT >& operator>>(
    std::basic_istream<CharT, TraitsT>& is, LogLevel& level)
{
    std::string tmp;
    is >> tmp;
    level = to_LogLevel(tmp);
    return is;
}

int g_indent = 0;

std::string DumpIndent()
{ return std::string(g_indent * 4, ' '); }

namespace {
    using TextFileSinkBackend  = sinks::text_file_backend;
    using TextFileSinkFrontend = sinks::synchronous_sink<TextFileSinkBackend>;

    // The backend should be accessible synchronously from any thread by any sink frontend
    boost::shared_ptr<TextFileSinkBackend>  f_file_sink_backend;
}

void InitLoggingSystem(const std::string& logFile, const std::string& _root_logger_name) {
    std::string root_logger_name = _root_logger_name;
    std::transform(root_logger_name.begin(), root_logger_name.end(), root_logger_name.begin(),
                   [](const char c) { return std::tolower(c); });

    // Register LogLevel so that the formatters will be found.
    logging::register_simple_formatter_factory<LogLevel, char>("Severity");
    logging::register_simple_filter_factory<LogLevel>("Severity");

    // Create a sink backend that logs to a file
    f_file_sink_backend = boost::make_shared<TextFileSinkBackend>(
        keywords::file_name = logFile.c_str(),
        keywords::auto_flush = true
    );

    // Create the frontend for formatting default records.
    auto file_sink_frontend = boost::make_shared<TextFileSinkFrontend>(f_file_sink_backend);

    // Create the format
    file_sink_frontend->set_formatter(
        expr::stream
        << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%H:%M:%S.%f")
        << " [" << log_severity << "] "
        << root_logger_name << " : " << log_src_filename << ":" << log_src_linenum << " : "
        << expr::message
    );

    // Set a filter to only use records from the default channel
    file_sink_frontend->set_filter(log_channel == "");

    logging::core::get()->add_sink(file_sink_frontend);

    // Add global attributes to all records
    logging::core::get()->add_global_attribute("TimeStamp", attr::local_clock());

    // Setup the OptionsDB options for the file sink.
    const std::string sink_option_name = "logging.sinks." + root_logger_name;
    GetOptionsDB().Add<std::string>(
        sink_option_name, UserStringNop("OPTIONS_DB_LOGGER_FILE_SINK_LEVEL"),
        to_string(default_sink_level), LogLevelValidator());

    // Use the option to set the threshold of the default logger
    LogLevel options_db_log_threshold = to_LogLevel(GetOptionsDB().Get<std::string>(sink_option_name));
    SetLoggerThreshold("", options_db_log_threshold);

    // Print setup message.
    auto date_time = std::time(nullptr);
    InternalLogger() << "Logger initialized at " << std::ctime(&date_time);
    InfoLogger() << FreeOrionVersionString();
}

void RegisterLoggerWithOptionsDB(const std::string& name) {
    if (name.empty())
        return;

    // Setup the OptionsDB options for this source.
    const std::string option_name = "logging.sources." + name;
    GetOptionsDB().Add<std::string>(
        option_name, UserStringNop("OPTIONS_DB_LOGGER_SOURCE_LEVEL"),
        "info", LogLevelValidator());
}
namespace {
    // Create a minimum severity table filter
    auto f_min_channel_severity = expr::channel_severity_filter(log_channel, log_severity);
}

void SetLoggerThreshold(const std::string& source, LogLevel threshold) {
    InternalLogger() << "Setting \"" << (source.empty() ? "default" : source)
                     << "\" logger threshold to \"" << threshold << "\".";

    logging::core::get()->reset_filter();
    f_min_channel_severity[source] = threshold;
    logging::core::get()->set_filter(f_min_channel_severity);
}

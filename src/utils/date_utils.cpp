/**
 * @author popmonkey+jres@gmail.com
 * @file src/utils/date_utils.cpp
 * @brief Utility functions for date and time handling.
 */
#include "utils/date_utils.hpp"

#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <vector>
#include <stdexcept>

// Cross-platform gmtime
std::tm* safe_gmtime(const std::time_t* timer, std::tm* buf) {
#if defined(_MSC_VER)
    gmtime_s(buf, timer);
    return buf;
#else
    return gmtime_r(timer, buf);
#endif
}

// Cross-platform timegm (inverse of gmtime)
std::time_t safe_timegm(std::tm* tm) {
#if defined(_MSC_VER)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

namespace jres {

    DateTime::DateTime() : m_timestamp(0) {}
    
    DateTime::DateTime(std::time_t timestamp) : m_timestamp(timestamp) {}

    DateTime DateTime::parse(const std::string& iso_str) {
        std::tm tm = {};
        std::istringstream ss(iso_str);
        
        // Try standard ISO format with T
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (ss.fail()) {
            // reset and try format with space
            ss.clear();
            ss.str(iso_str);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            if (ss.fail()) {
                throw std::runtime_error("Invalid date-time format: " + iso_str);
            }
        }
        
        // Check for remaining characters which are not a 'Z' or '.000Z'
        std::string remaining;
        ss >> remaining;
        if (!remaining.empty() && remaining != "Z" && remaining != ".000Z") {
            // A non-empty remaining string is only ok if it's just whitespace
            if (remaining.find_first_not_of(" \t\n\v\f\r") != std::string::npos) {
                 throw std::runtime_error("Invalid trailing characters in date-time: " + iso_str);
            }
        }
        
        return DateTime(safe_timegm(&tm));
    }

    DateTime DateTime::add_seconds(long long seconds) const {
        return DateTime(m_timestamp + seconds);
    }

    DateTime DateTime::add_hours(int hours) const {
        return DateTime(m_timestamp + (hours * 3600));
    }

    double DateTime::diff_seconds(const DateTime& other) const {
        return std::difftime(m_timestamp, other.m_timestamp);
    }

    std::string DateTime::to_string() const {
        std::tm tm_buf;
        safe_gmtime(&m_timestamp, &tm_buf);
        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string DateTime::date_string() const {
        std::tm tm_buf;
        safe_gmtime(&m_timestamp, &tm_buf);
        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d");
        return ss.str();
    }

    std::string DateTime::time_string() const {
        std::tm tm_buf;
        safe_gmtime(&m_timestamp, &tm_buf);
        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%H:%M");
        return ss.str();
    }

    std::string DateTime::format_duration(long long total_seconds) {
        long long hours = total_seconds / 3600;
        long long remainder = total_seconds % 3600;
        long long minutes = remainder / 60;

        std::vector<std::string> parts;
        if (hours > 0) {
            parts.push_back(std::to_string(hours) + " hour" + (hours != 1 ? "s" : ""));
        }
        if (minutes > 0) {
            parts.push_back(std::to_string(minutes) + " minute" + (minutes != 1 ? "s" : ""));
        }

        if (parts.empty()) return "";
        
        std::string result = "for ";
        if (parts.size() == 1) {
            result += parts[0];
        } else {
            result += parts[0] + " and " + parts[1];
        }
        return result;
    }

    std::vector<std::string> DateTime::get_date_range(const DateTime& start, const DateTime& end) {
        std::vector<std::string> dates;
        
        // Normalize to start of day
        std::tm tm_buf;
        safe_gmtime(&start.m_timestamp, &tm_buf);
        tm_buf.tm_hour = 0; tm_buf.tm_min = 0; tm_buf.tm_sec = 0;
        
        std::time_t current = safe_timegm(&tm_buf);
        std::time_t end_t = end.m_timestamp;

        // Loop until current day is past end day
        // Note: We compare formatted strings to handle "next day" logic simply
        while (current <= end_t + 86400) { // buffer to ensure last day is covered
            DateTime dt(current);
            std::string ds = dt.date_string();
            dates.push_back(ds);
            
            if (ds == end.date_string()) break;
            
            current += 86400; // Add 24 hours
        }
        return dates;
    }

}

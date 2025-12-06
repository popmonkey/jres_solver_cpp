/**
 * @author popmonkey+jres@gmail.com
 * @file src/utils/date_utils.hpp
 * @brief Utility functions for date and time handling.
 */

 #pragma once

#include <string>
#include <chrono>
#include <vector>

namespace jres {

    /**
     * A simplified DateTime wrapper to handle UTC timestamps,
     * duration arithmetic, and Timezone offset formatting.
     */
    class DateTime {
    public:
        // Constructors
        DateTime();
        explicit DateTime(std::time_t timestamp);
        
        // Parse an ISO 8601 string (e.g., "1973-06-09T14:37:00.000Z" or "1973-06-09 14:37:00")
        static DateTime parse(const std::string& iso_str);

        // Arithmetic
        DateTime add_seconds(long long seconds) const;
        DateTime add_hours(int hours) const;
        double diff_seconds(const DateTime& other) const;

        // Formatting
        // Returns format "YYYY-MM-DD HH:MM:SS"
        std::string to_string() const;
        
        // Returns format "YYYY-MM-DD"
        std::string date_string() const;

        // Returns format "HH:MM"
        std::string time_string() const;

        // Comparison
        bool operator<(const DateTime& other) const { return m_timestamp < other.m_timestamp; }
        bool operator>(const DateTime& other) const { return m_timestamp > other.m_timestamp; }
        bool operator==(const DateTime& other) const { return m_timestamp == other.m_timestamp; }
        bool operator<=(const DateTime& other) const { return m_timestamp <= other.m_timestamp; }
        bool operator>=(const DateTime& other) const { return m_timestamp >= other.m_timestamp; }

        // Accessors
        std::time_t get_timestamp() const { return m_timestamp; }
        
        // Format a duration in seconds into "X hours and Y minutes"
        static std::string format_duration(long long total_seconds);

        // Returns list of dates between start and end (inclusive) as strings "YYYY-MM-DD"
        static std::vector<std::string> get_date_range(const DateTime& start, const DateTime& end);

    private:
        std::time_t m_timestamp;
    };

}

#pragma once
#include "../jres_internal_types.hpp"

namespace jres::internal {

    struct CapacityAnalysis {
        int totalCapacity;
        std::string details;
    };

    class CapacityAnalyzer {
    public:
        static CapacityAnalysis calculate_max_potential_capacity(
            const std::vector<TeamMember>& participants,
            const SolverInput& input
        );
    };

}

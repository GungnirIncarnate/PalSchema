#pragma once

#include "File/Macros.hpp"

namespace Palworld::StringHelpers {
    void Replace(RC::StringType& string, const RC::StringType& match, const RC::StringType& replaceWith) {
        string.replace(string.find(match), match.size(), replaceWith);
    }
}
#pragma once

// --------------------------------------------------
// fix_literals.h
// WSTRZYKIWANY przez /FI
// MUSI być bezpieczny dla STL
// --------------------------------------------------

#include <string>
#include <string_view>

// --------------------------------------------------
// Tryb bezpieczny (domyślny)
// --------------------------------------------------
namespace saf_literals
{
    using namespace std::string_literals;
    using namespace std::string_view_literals;
}

// --------------------------------------------------
// Tryb kompatybilny z CommonLibSF
//
// AKTYWOWANY TYLKO GDY jawnie zdefiniowane:
//   ENABLE_GLOBAL_STRING_LITERALS
// --------------------------------------------------
#if defined(ENABLE_GLOBAL_STRING_LITERALS)
    using namespace std::string_literals;
    using namespace std::string_view_literals;
#endif

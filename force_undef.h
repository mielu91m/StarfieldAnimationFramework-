// force_undef.h
// Ten plik jest automatycznie includowany PRZED każdym innym plikiem
// Globalnie wyłącza szkodliwe makra Windows

#pragma once

// Podstawowe makra Windows które kolidują z CommonLibSF
#ifdef NEAR
#undef NEAR
#endif

#ifdef FAR
#undef FAR
#endif

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#ifdef GetObject
#undef GetObject
#endif

#ifdef SendMessage
#undef SendMessage
#endif

#ifdef GetClassName
#undef GetClassName
#endif

#ifdef ERROR
#undef ERROR
#endif

// Makra z spdlog/logging
#ifdef TRACE
#undef TRACE
#endif

#ifdef DEBUG
#undef DEBUG
#endif

#ifdef INFO
#undef INFO
#endif

#ifdef WARN
#undef WARN
#endif

#ifdef FATAL
#undef FATAL
#endif

// Inne problematyczne makra
#ifdef DELETE
#undef DELETE
#endif

#ifdef IN
#undef IN
#endif

#ifdef OUT
#undef OUT
#endif

#ifdef ABSOLUTE
#undef ABSOLUTE
#endif

#ifdef RELATIVE
#undef RELATIVE
#endif

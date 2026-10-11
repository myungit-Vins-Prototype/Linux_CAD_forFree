# Impronta dei sorgenti che calcolano la geometria (kernel e funzioni
# dell'app): la copia dei corpi salvata nei documenti vale solo se e' stata
# calcolata con questi stessi sorgenti. Uso:
#   cmake -DOUT=<header> -DLIST=<file con l'elenco dei sorgenti> -P source_hash.cmake
# Riscrive l'header solo se l'impronta cambia (niente ricompilazioni inutili).
file(STRINGS "${LIST}" sources)
set(all "")
foreach(source IN LISTS sources)
    file(SHA256 "${source}" hash)
    string(APPEND all "${hash}")
endforeach()
string(SHA256 digest "${all}")
set(content "// Generato da cmake/source_hash.cmake: non modificare.\n#define FORGECAD_SOURCE_HASH \"${digest}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" previous)
else()
    set(previous "")
endif()
if(NOT previous STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()

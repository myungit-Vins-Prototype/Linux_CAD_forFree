# Impronta dei sorgenti che calcolano la geometria (kernel e funzioni
# dell'app): la copia dei corpi salvata nei documenti vale solo se e' stata
# calcolata con questi stessi sorgenti. Uso:
#   cmake -DOUT=<header> -DLIST=<file con l'elenco dei sorgenti> -P source_hash.cmake
# Riscrive l'header solo se l'impronta cambia (niente ricompilazioni inutili).
file(STRINGS "${LIST}" sources)
set(all "")
foreach(source IN LISTS sources)
    get_filename_component(source_name "${source}" NAME)
    if(source_name STREQUAL "forgeCad2026_gui.cpp")
        # Il file GUI contiene anche il dispatcher che costruisce i B-rep.
        # Hashiamo soltanto quella sezione: colori, pannelli e altre modifiche
        # dell'interfaccia non rendono inutilizzabile la cache geometrica.
        file(READ "${source}" source_content)
        set(begin_marker "// FORGECAD_GEOMETRY_HASH_BEGIN")
        set(end_marker "// FORGECAD_GEOMETRY_HASH_END")
        string(FIND "${source_content}" "${begin_marker}" begin)
        string(FIND "${source_content}" "${end_marker}" end)
        if(begin LESS 0 OR end LESS 0 OR end LESS_EQUAL begin)
            message(FATAL_ERROR "Marcatori dell'impronta geometrica mancanti in ${source}")
        endif()
        string(LENGTH "${begin_marker}" marker_length)
        math(EXPR begin "${begin} + ${marker_length}")
        math(EXPR section_length "${end} - ${begin}")
        string(SUBSTRING "${source_content}" ${begin} ${section_length} geometry_section)
        string(SHA256 hash "${geometry_section}")
    else()
        file(SHA256 "${source}" hash)
    endif()
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

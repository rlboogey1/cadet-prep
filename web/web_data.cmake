# Makes the question folder that ships to the browser: a copy of data/ without
# Warrior Knowledge (the handbook cards, the insignia pictures, and its sections
# in subtests.json). Run by the build:  cmake -DSRC=data -DDST=out -P web_data.cmake
file(REMOVE_RECURSE "${DST}")
file(COPY "${SRC}/" DESTINATION "${DST}"
     PATTERN "warrior_cards.json" EXCLUDE
     PATTERN "insignia" EXCLUDE
     PATTERN ".DS_Store" EXCLUDE)

file(READ "${SRC}/subtests.json" _json)
string(JSON _count LENGTH "${_json}" subtests)
math(EXPR _last "${_count} - 1")
foreach(_i RANGE ${_last} 0 -1)  # backwards, so removing one doesn't move the rest
    string(JSON _track ERROR_VARIABLE _none GET "${_json}" subtests ${_i} track)
    if(_track STREQUAL "warrior")
        string(JSON _json REMOVE "${_json}" subtests ${_i})
    endif()
endforeach()
file(WRITE "${DST}/subtests.json" "${_json}")

# Fail the build rather than publish the handbook by accident
file(GLOB_RECURSE _files "${DST}/*")
foreach(_f ${_files})
    if(_f MATCHES "\\.json$")
        file(READ "${_f}" _text)
        string(TOLOWER "${_text}" _text)
        if(_text MATCHES "warrior|wk_block")
            message(FATAL_ERROR "Warrior Knowledge found in the website's data: ${_f}")
        endif()
    endif()
endforeach()

# End-to-end: synth -> track -> eval -> nuke, through the real CLI and files on disk.
# Invoked by CTest with -DPTRACK=<exe> -DWORK=<dir>.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "command failed (${rc}): ${ARGN}\n${out}\n${err}")
  endif()
  set(LAST_OUT "${out}" PARENT_SCOPE)
endfunction()

run("${PTRACK}" synth --out "${WORK}" --frames 12 --ref-offset 4 --noise 0.005)
file(READ "${WORK}/truth.json" truth)
string(JSON surface GET "${truth}" surface)
set(s "")
foreach(i RANGE 3)
  foreach(j RANGE 1)
    string(JSON v GET "${surface}" ${i} ${j})
    list(APPEND s "${v}")
  endforeach()
endforeach()
string(JOIN "," s ${s})

run("${PTRACK}" track --seq "${WORK}/plate.####.exr" --ref 1005 --mask "${WORK}/mask.exr"
    --surface "${s}" --out "${WORK}/track.json" --nuke "${WORK}/pin.nk" --quiet)
run("${PTRACK}" eval --track "${WORK}/track.json" --truth "${WORK}/truth.json")
string(REGEX MATCH "mean corner error ([0-9.]+)" _ "${LAST_OUT}")
if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 GREATER 0.1)
  message(FATAL_ERROR "corner error too high: ${LAST_OUT}")
endif()
message(STATUS "${LAST_OUT}")

file(READ "${WORK}/pin.nk" pin)
foreach(knob "CornerPin2D {" " to1 {{curve x1001" " from4 {")
  string(FIND "${pin}" "${knob}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "pin.nk is missing '${knob}'")
  endif()
endforeach()

# Same shot as a ProRes movie, numbered from 1001, when ffmpeg is available.
find_program(FFMPEG ffmpeg)
if(FFMPEG)
  execute_process(COMMAND "${FFMPEG}" -v error -y -framerate 24 -start_number 1001 -i "${WORK}/plate.%04d.exr"
                          -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le "${WORK}/clip.mov"
                  RESULT_VARIABLE rc)
  if(rc EQUAL 0)
    run("${PTRACK}" track --seq "${WORK}/clip.mov" --start 1001 --ref 1005 --mask "${WORK}/mask.exr"
        --surface "${s}" --out "${WORK}/track_mov.json" --quiet)
    run("${PTRACK}" eval --track "${WORK}/track_mov.json" --truth "${WORK}/truth.json")
    string(REGEX MATCH "mean corner error ([0-9.]+)" _ "${LAST_OUT}")
    if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 GREATER 0.1)
      message(FATAL_ERROR "movie corner error too high: ${LAST_OUT}")
    endif()
    message(STATUS "movie: ${LAST_OUT}")
  endif()
endif()

# Overscan: plates with data outside the display window. Tracking with and
# without --overscan must agree with the display-window ground truth.
set(OS "${WORK}/overscan")
run("${PTRACK}" synth --out "${OS}" --frames 10 --overscan 32 --motion 2)
foreach(flag "" "--overscan")
  run("${PTRACK}" track --seq "${OS}/plate.####.exr" --mask "${OS}/mask.exr" --out "${OS}/t.json" --quiet ${flag})
  run("${PTRACK}" eval --track "${OS}/t.json" --truth "${OS}/truth.json")
  string(REGEX MATCH "mean corner error ([0-9.]+)" _ "${LAST_OUT}")
  if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 GREATER 0.05)
    message(FATAL_ERROR "overscan track (${flag}) corner error too high: ${LAST_OUT}")
  endif()
  message(STATUS "overscan ${flag}: ${LAST_OUT}")
endforeach()

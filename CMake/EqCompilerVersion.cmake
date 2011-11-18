# Copyright (c) 2011 Stefan Eilemann <eile@eyescale.ch>

FUNCTION(EQ_COMPILER_DUMPVERSION OUTPUT_VERSION)
  EXECUTE_PROCESS(COMMAND
    ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_ARG1} -dumpversion
    OUTPUT_VARIABLE EQ_COMPILER_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  STRING(REGEX REPLACE "([0-9])\\.([0-9])(\\.[0-9])?" "\\1\\2"
    EQ_COMPILER_VERSION ${EQ_COMPILER_VERSION})

  SET(${OUTPUT_VERSION} ${EQ_COMPILER_VERSION} PARENT_SCOPE)
ENDFUNCTION()

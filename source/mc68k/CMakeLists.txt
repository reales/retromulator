cmake_minimum_required(VERSION 3.16)

# Set the project name
project(68kEmu)
  
# Add sub directories
add_library(68kEmu STATIC)

set(SOURCES_MUSASHI
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68k.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kconf.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kcpu.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kcpu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kdasm.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kfpu.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kmmu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kops.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kops.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/mamesf.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/milieu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/softfloat.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/softfloat.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/softfloat-macros
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/softfloat-specialize
)

set(SOURCES
	cpuState.h
	gpt.cpp gpt.h
	hdi08.cpp hdi08.h
	hdi08periph.h
	logging.cpp logging.h
	mc68k.cpp mc68k.h
	musashiEntry.h
	peripheralBase.cpp peripheralBase.h
	peripheralTypes.h
	port.cpp port.h
	qsm.cpp qsm.h
	qspi.cpp qspi.h
	sim.cpp sim.h
)

target_sources(68kEmu PRIVATE ${SOURCES} ${SOURCES_MUSASHI})
source_group("source" FILES ${SOURCES})
source_group("source\\Musashi" FILES ${SOURCES_MUSASHI})

set_property(TARGET 68kEmu PROPERTY CXX_STANDARD 17)

set( PTE_EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external )

set( PTE_DEV_BIN_DIR ${CMAKE_BINARY_DIR}/bin )
set( PTE_DATA_DIR data )

if ( PLATFORM_LINUX )
    set( PTE_DATA_INSTALL_DIR share/powertab/powertabeditor )
else ()
    set( PTE_DATA_INSTALL_DIR data )
endif ()
set(CKRASTERIZERLIB_INC_DIR ${PROJECT_SOURCE_DIR}/include)

set(CKRASTERIZERLIB_PUBLIC_HDRS
        ${CKRASTERIZERLIB_INC_DIR}/CKRasterizerEnums.h
        ${CKRASTERIZERLIB_INC_DIR}/CKRasterizerTypes.h
        ${CKRASTERIZERLIB_INC_DIR}/CKRasterizer.h
        ${CKRASTERIZERLIB_INC_DIR}/RasterizersTextureDesc.h
        )

set(CKRASTERIZERLIB_PRIVATE_HDRS
        )

set(CKRASTERIZERLIB_SRCS
        CKRasterizer.cpp
        CKRasterizerDriver.cpp
        CKRasterizerContext.cpp
        )

add_library(CKRasterizerLib STATIC ${CKRASTERIZERLIB_SRCS} ${CKRASTERIZERLIB_PUBLIC_HDRS} ${CKRASTERIZERLIB_PRIVATE_HDRS})
target_include_directories(CKRasterizerLib PUBLIC
        $<BUILD_INTERFACE:${CKRASTERIZERLIB_INC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )
target_link_libraries(CKRasterizerLib PUBLIC CK2 VxMath)

add_library(CKNULLRasterizer SHARED ${CKRASTERIZERLIB_SRCS} ${CKRASTERIZERLIB_PUBLIC_HDRS} ${CKRASTERIZERLIB_PRIVATE_HDRS})
set_target_properties(CKNULLRasterizer PROPERTIES DEFINE_SYMBOL CKNULLRASTERIZER_DLL)
target_include_directories(CKNULLRasterizer PRIVATE ${CKRASTERIZERLIB_INC_DIR})
target_link_libraries(CKNULLRasterizer PRIVATE CK2 VxMath)

foreach (LIB IN ITEMS CKRasterizerLib CKNULLRasterizer)
    # Disable msvc unsafe warnings
    target_compile_definitions(${LIB} PRIVATE
            $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
            $<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>
            )
endforeach ()

configure_package_config_file("${PROJECT_SOURCE_DIR}/CMake/CKRasterizerLibConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CKRasterizerLibConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CKRasterizerLib"
        )

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CKRasterizerLibConfigVersion.cmake"
        COMPATIBILITY SameMinorVersion
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CKRasterizerLibConfig.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CKRasterizerLib"
        )

install(TARGETS CKRasterizerLib
        EXPORT CKRasterizerLibTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )

install(TARGETS CKNULLRasterizer
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )

install(EXPORT "CKRasterizerLibTargets"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CKRasterizerLib")

install(FILES ${CKRASTERIZERLIB_PUBLIC_HDRS}
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
# Include after arcade-shelf and its QML module have been defined.
include(GNUInstallDirs)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/arcade-shelf/licenses")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/licenses/" DESTINATION "${CMAKE_INSTALL_DATADIR}/arcade-shelf/licenses" OPTIONAL)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/vendor/arcade-link/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/arcade-shelf/licenses/arcade-link"
        FILES_MATCHING PATTERN "LICENSE*" PATTERN "provenance.json")
if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/arcade.shelf.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/io.github.qa_p1.ArcadeShelf.metainfo.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/arcade-shelf.svg"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
            RENAME io.github.qa_p1.ArcadeShelf.svg)
endif()
set(CPACK_PACKAGE_NAME "Arcade-Shelf")
set(CPACK_PACKAGE_VENDOR "Arcade contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A floating staging workspace for desktop content")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/qa-p1/Arcade-Shelf")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Arcade Shelf")
if(WIN32)
    # The dedicated NSIS script uses HKCU, RequestExecutionLevel user and
    # windeployqt. CPack's stock NSIS template requires administrator access.
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Arcade Shelf")
else()
    # CPack TGZ is a developer/system-Qt package. The portable runtime bundle
    # is built by scripts/package-linux.py and has a distinct asset name.
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

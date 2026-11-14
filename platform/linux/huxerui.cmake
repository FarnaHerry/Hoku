function(huxerui_configure_linux_project_package target_name install_component)
    if (NOT HUXERUI_PACKAGE)
        return()
    endif ()

    set(package_directory usr)
    if (HUXERUI_LINUX_SYSTEM_PACKAGE)
        set(package_directory "usr/lib/dev.farna.hoku")
    endif ()
    install(TARGETS ${target_name}
            RUNTIME DESTINATION "${package_directory}/bin"
            COMPONENT "${install_component}"
    )
    # HuxerUI's Linux package is the application payload only. GTK, WebKitGTK,
    # and their dependency closure are distribution runtimes: DEB/RPM declare
    # them in package metadata and AppImage documents them as host requirements.
    # Deploying the closure here duplicated WebKitGTK (and over 200 MB of
    # private libraries) inside every package.
    get_target_property(HUXERUI_LINUX_APP_RESOURCES
            ${target_name}
            HUXERUI_RESOURCE_PACKAGE
    )
    if (HUXERUI_LINUX_APP_RESOURCES
            AND NOT HUXERUI_LINUX_APP_RESOURCES MATCHES "-NOTFOUND$")
        install(DIRECTORY "${HUXERUI_LINUX_APP_RESOURCES}/"
                DESTINATION "${package_directory}/bin/$<TARGET_FILE_BASE_NAME:${target_name}>.resources"
                COMPONENT "${install_component}"
        )
    endif ()

    if (HUXERUI_LINUX_SYSTEM_PACKAGE)
        file(READ "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package/hoku.desktop" desktop_entry)
        string(REGEX REPLACE "(^|\n)Exec=[^\n]*" "\\1Exec=/${package_directory}/bin/hoku"
                desktop_entry "${desktop_entry}")
        string(REGEX REPLACE "(^|\n)Icon=[^\n]*" "\\1Icon=dev.farna.hoku" desktop_entry "${desktop_entry}")
        set(installed_desktop_entry "${CMAKE_CURRENT_BINARY_DIR}/huxerui-package/$<CONFIG>/dev.farna.hoku.desktop")
        file(GENERATE OUTPUT "${installed_desktop_entry}" CONTENT "${desktop_entry}")
        install(FILES "${installed_desktop_entry}" DESTINATION usr/share/applications COMPONENT "${install_component}")
        install(FILES "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package/hoku.svg"
                DESTINATION usr/share/icons/hicolor/scalable/apps
                RENAME "dev.farna.hoku.svg" COMPONENT "${install_component}")
        file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/huxerui-package/LinuxCPack.cmake" CONTENT [=[
set(CPACK_PACKAGE_NAME "dev.farna.hoku")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Hoku lightweight desktop browser")
set(CPACK_PACKAGE_DESCRIPTION "Hoku lightweight desktop browser")
set(CPACK_PACKAGE_VENDOR "FarnaHerry")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/FarnaHerry/Hoku")
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_PACKAGE_RELOCATABLE OFF)

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "dev.farna.hoku")
set(CPACK_DEBIAN_PACKAGE_SECTION "web")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libgtk-4-1 (>= 4.14), libwebkitgtk-6.0-4, librsvg2-common")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_RPM_PACKAGE_DESCRIPTION "Hoku lightweight desktop browser")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_REQUIRES "gtk4 >= 4.14, webkitgtk6.0, librsvg2")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
        /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps)
]=] @ONLY)
        return()
    endif ()

    install(PROGRAMS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package/AppRun"
            DESTINATION .
            COMPONENT "${install_component}"
    )
    install(FILES
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package/hoku.desktop"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package/hoku.svg"
            DESTINATION .
            COMPONENT "${install_component}"
    )
endfunction()

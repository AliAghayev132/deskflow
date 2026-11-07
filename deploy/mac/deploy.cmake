# SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT

# HACK This is set when the files is included so its the real path
# calling CMAKE_CURRENT_LIST_DIR after include would return the wrong scope var
set(MY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(OSX_BUNDLE ${BUILD_OSX_BUNDLE})

set(OS_STRING "macos-${BUILD_ARCHITECTURE}")

if (OSX_BUNDLE)
  if (APPLE_CODESIGN_DEV)
    install(CODE "execute_process(COMMAND
      ${DEPLOYQT}
      \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
      -hardened-runtime -timestamp \"-codesign=${APPLE_CODESIGN_DEV}\"
    )")
  else()
    # Unsigned builds (forks, pull requests): the hardened runtime enforces library validation, so an
    # ad-hoc signed app refuses to load bundled frameworks signed by another team and crashes at launch.
    # Notarization needs a real identity anyway, so leave the hardened runtime out and give every
    # binary in the bundle the same ad-hoc signature.
    install(CODE "execute_process(COMMAND
      ${DEPLOYQT}
      \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
      \"-codesign=-\"
    )")
    install(CODE "execute_process(COMMAND
      /usr/bin/codesign --force --deep --sign -
      \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
    )")
  endif()
  set(CPACK_PACKAGE_ICON "${MY_DIR}/dmg-volume.icns")
  set(CPACK_DMG_BACKGROUND_IMAGE "${MY_DIR}/dmg-background.tiff")
  set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${MY_DIR}/generate_ds_store.applescript")
  set(CPACK_DMG_VOLUME_NAME "${CMAKE_PROJECT_PROPER_NAME}")
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE ON)
  set(CPACK_GENERATOR "DragNDrop")
endif()

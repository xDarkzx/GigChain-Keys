# The product's name and identity: the ONE place to change it.
#
# Everything the user sees comes from here: window titles, the splash
# screen, messages, the Windows settings and log folders, the setlist file
# extension, the executable's name and the name other apps see for our MIDI
# and audio connections. Edit, rebuild, done.
#
# Internal code names (the C++ namespace `gigchain`, target and folder
# names) are not branding and do not change with it.

set(PRODUCT_BRAND "GigChain")                  # the family name
set(PRODUCT_EDITION "Keys")                    # this product in the family
set(PRODUCT_NAME "${PRODUCT_BRAND} ${PRODUCT_EDITION}")
set(PRODUCT_VERSION "0.4.0")

# Windows settings live under HKCU\Software\<ORGANIZATION>\<PRODUCT_NAME>,
# logs and caches under %LOCALAPPDATA%\<ORGANIZATION>\<PRODUCT_NAME>.
set(PRODUCT_ORGANIZATION "${PRODUCT_BRAND}")

set(PRODUCT_EXECUTABLE "GigChainKeys")         # GigChainKeys.exe
set(PRODUCT_BUNDLE_ID "nz.dkstudios.gigchainkeys") # the Mac's app id: never changes after the first release
set(PRODUCT_FILE_EXTENSION "gigchain")         # setlists: <name>.gigchain.json
set(PRODUCT_WEBSITE "https://github.com/xDarkzx/GigChain-Keys")

# The splash screen picture. The name is part of this picture, so a rename
# needs a new picture too. Rounded corners should be transparent.
set(PRODUCT_SPLASH_IMAGE "${CMAKE_CURRENT_LIST_DIR}/branding/splash.png")
set(PRODUCT_BRAND_DIR "${CMAKE_CURRENT_LIST_DIR}/branding") # images and the Mac's Info.plist

# The app's icon (the .exe in Explorer, the taskbar, its windows), made from
# branding/gigchain-icon.png by tools/make-icon.ps1.
set(PRODUCT_ICON "${CMAKE_CURRENT_LIST_DIR}/branding/app.ico")

# What Windows shows in the .exe's Properties and in Apps & Features, and the
# installer's wording.
set(PRODUCT_DESCRIPTION "${PRODUCT_NAME} - live keyboard rig host")
set(PRODUCT_PUBLISHER "${PRODUCT_BRAND}")
set(PRODUCT_COPYRIGHT "Copyright (C) 2026 ${PRODUCT_BRAND} contributors. GPL-3.0-or-later.")
# The installer's identity: upgrades find the installed copy by it. Never change it.
set(PRODUCT_INSTALLER_ID "74122740-AA8A-4712-9184-00E0369F6041")

# Earlier names. Settings saved under them are carried over on first start,
# and a test checks the source never mentions them. On a rename, add the
# current values here.
set(PRODUCT_PREVIOUS_SETTINGS "OpenStage/OpenStage")   # organization/application
set(PRODUCT_PREVIOUS_FILE_EXTENSIONS "openstage")      # old setlists: <name>.openstage.json

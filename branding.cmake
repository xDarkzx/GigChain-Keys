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
set(PRODUCT_VERSION "0.1.0")

# Windows settings live under HKCU\Software\<ORGANIZATION>\<PRODUCT_NAME>,
# logs and caches under %LOCALAPPDATA%\<ORGANIZATION>\<PRODUCT_NAME>.
set(PRODUCT_ORGANIZATION "${PRODUCT_BRAND}")

set(PRODUCT_EXECUTABLE "GigChainKeys")         # GigChainKeys.exe
set(PRODUCT_FILE_EXTENSION "gigchain")         # setlists: <name>.gigchain.json
set(PRODUCT_WEBSITE "https://github.com/xDarkzx/GigChain-Keys")

# The splash screen picture. The name is part of this picture, so a rename
# needs a new picture too. Rounded corners should be transparent.
set(PRODUCT_SPLASH_IMAGE "${CMAKE_CURRENT_LIST_DIR}/branding/splash.png")

# Earlier names. Settings saved under them are carried over on first start,
# and a test checks the source never mentions them. On a rename, add the
# current values here.
set(PRODUCT_PREVIOUS_SETTINGS "OpenStage/OpenStage")   # organization/application
set(PRODUCT_PREVIOUS_FILE_EXTENSIONS "openstage")      # old setlists: <name>.openstage.json

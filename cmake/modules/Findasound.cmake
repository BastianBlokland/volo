# --------------------------------------------------------------------------------------------------
# CMake utility to find the Alsa (https://www.alsa-project.org) library.
# --------------------------------------------------------------------------------------------------

#
# 'Advanced Linux Sound Architecture' (ALSA) audio library.
# For debian based systems: apt install libasound2-dev libasound2
# More info: https://alsa-project.org
#

findpkg(LIB "asound" HEADER "alsa/asoundlib.h")

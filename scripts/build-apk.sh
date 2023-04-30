cd srceng-androidwaf
git clone https://gitlab.com/LostGamer/android-sdk
export ANDROID_SDK_HOME=$PWD/android-sdk
export MODNAME=missinginfo
export MODNAMESTRING='Missing Information (Beta Build)'
git pull
./mod.sh
./waf configure -T release &&
./waf build

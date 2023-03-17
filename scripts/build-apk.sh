cd srceng-androidwaf
git clone https://gitlab.com/LostGamer/android-sdk
export ANDROID_SDK_HOME=$PWD/android-sdk
git pull
./waf configure -T release &&
./waf build

// Helper for tst_Unpacked: calls arcade_relaunch_unpacked("arcade.test", "1.2.3", $PROBE_WAIT != "0", argv) the way an
// app's main does. The test copies this executable into a fake AppDir so /proc/self/exe is inside $APPDIR.
// Exits with the returned code, or 42 when the call returned -1 (the app would keep running).
#include "arcade_unpacked.h"
#include <cstdlib>
#include <cstring>

int main(int, char **argv)
{
    const char *wait = std::getenv("PROBE_WAIT");
    const int code = arcade_relaunch_unpacked("arcade.test", "1.2.3", !(wait && std::strcmp(wait, "0") == 0), argv);
    return code == -1 ? 42 : code;
}

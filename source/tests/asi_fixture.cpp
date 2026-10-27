#include <windows.h>
extern "C" __declspec(dllimport) int FixtureValue();
static int value = 0;
extern "C" __declspec(dllexport) void InitializeASI() {
    Sleep(100); // Make transient process-wide working-directory changes observable.
    value = FixtureValue();
}
extern "C" __declspec(dllexport) int GetFixtureResult() { return value; }

#include "ScreenRecord.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif
int main(int argc, char* argv[]) {
    ScreenRecord* screenRecord = new ScreenRecord();
    screenRecord->Init();
    screenRecord->Start();
    int x;
    std::cin >> x;
    screenRecord->Stop();
    while(!screenRecord->GetFinito()) {
        sleep(0.2);
    }
    return 0;
}
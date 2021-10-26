#include "ScreenRecord.h"
#ifdef _WIN32
    #include <Windows.h>
#else
    #include <unistd.h>
#endif

#define KEY_PAUSE   112
#define KEY_RESUME  114
#define KEY_STOP    115

int main(int argc, char* argv[]) {
    if(argc != 6) {
        std::cout << "Wrong number of arguments" << std::endl;
        return 0;
    }
    int key = 0;
    auto* screenRecord = new ScreenRecord();
    screenRecord->Init(argv[1], (int) strtol(argv[2], nullptr, 10), (int) strtol(argv[3], nullptr, 10), argv[4], argv[5]);
    screenRecord->Start();

    while(key != KEY_STOP){
        key = getchar();
        switch(key)
        {
            case KEY_PAUSE:     screenRecord->Pause();  break;
            case KEY_RESUME:    screenRecord->Start();  break;
            case KEY_STOP:
                screenRecord->Stop();
                while(!screenRecord->isDone);
                break;
            default: break;
        }
    }

#ifdef _WIN32
    Sleep(2000);
#else
    sleep(2);
#endif
    return 0;
}
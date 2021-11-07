#include "ScreenRecord.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#define KEY_PAUSE 112
#define KEY_RESUME 114
#define KEY_STOP 115

int main(int argc, char *argv[])
{
    std::cout << "Parte" << std::endl;
    int key = 0;
    ScreenRecord *screenRecord = new ScreenRecord();
    std::cout << "New ScreenRecord fatta" << std::endl;
    screenRecord->Init(argv[1], (int)strtol(argv[2], NULL, 10), (int)strtol(argv[3], NULL, 10), argv[4], argv[5]);
    std::cout << "Init fatta" << std::endl;
    screenRecord->Start();

    while (key != KEY_STOP)
    {
        key = getchar();

        switch (key)
        {
        case KEY_PAUSE:
            screenRecord->Pause();
            break;
        case KEY_RESUME:
            screenRecord->Start();
            break;
        case KEY_STOP:
            screenRecord->Stop();
            while (!screenRecord->isDone)
            {
            };
            break;
        default:
            break;
        }
    }

    sleep(2);

    return 0;
}
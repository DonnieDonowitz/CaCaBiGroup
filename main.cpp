#include "ScreenRecord.h"

static std::string toUpperCase(std::string src) {
    std::string dst = "";
    for(char const &c: src) {
        dst += toupper(c);
    }
    return dst;
}

int main(int argc, char** argv)
{
    int width, widthOffset, height, heightOffset;
    char c;
    bool record;
    std::string filename;
    ScreenRecord* capture;

    std::cout
    << "======================================================================================================================" << std::endl
    << "================================================ SCREEN-AUDIO CAPTURE ================================================" << std::endl
    << "======================================================================================================================" << std::endl << std::endl
    << "Developed for project of \"Programmazione di Sistema\"" << std::endl
    << "It allows to record desktop screen (optionally with audio) in customizable size and save output video on specified file" << std::endl
    << "Authors: Angelo Marino Carmollingo - Matteo Biffoni - Simone Cavallo" << std::endl << std::endl;


   
    std::cout << "Type the width of the recording: ";
    std::cin >> width;

    std::cout << "Type the width offset of the recording: ";
    std::cin >> widthOffset;

    std::cout << "Type the height of the recording: ";
    std::cin >> height;

    std::cout << "Type the height offset of the recording: ";
    std::cin >> heightOffset;

    std::cout << "Type the output file name (without .mp4 extension): ";
    std::cin >> filename;

    std::cout << "Do you want to record audio? (y/n) ";
    std::cin >> c;

    if(c == 'y' || c == 'n' || c == 'Y' || c == 'N')
    {
        if(c == 'y' || c == 'Y')
        {
            capture = new ScreenRecord(filename.append(".mp4"), argv[1], argv[2], true);
        }
        else
        {
            capture = new ScreenRecord(filename.append(".mp4"), argv[1], argv[2], false);
        }
    } 
    else
    {
        std::cout << "Wrong input." << std::endl;
    }

    capture->SetDimensions(width, widthOffset, height, heightOffset);
    capture->PrintDimensions();

    try
    {
        std::cout << std::endl << std::endl;
        std::cout << "Type 'start' to start recording, then available commands will be 'pause', 'resume' and 'stop'." << std::endl << std::endl;    

        while(true)
        {
            try
            {
                std::string command;
                std::getline(std::cin, command);
                std::cout << std::endl;
                command = toUpperCase(command);
             
                if(command == "START")
                {
                    capture->Start();
                }
                else if(command == "RESUME")
                {
                    capture->Resume();
                }
                else if(command == "PAUSE")
                {
                    capture->Pause();
                }
                else if(command == "STOP")
                {
                    capture->Stop();
                    std::cout << "Cleaning up remaining data..." << std::endl << std::endl;
                    while(!capture->hasFinished());
                    break;
                }
                else
                {
                    std::cout << "Invalid command, try again." << std::endl;
                }

                sleep(2);
            }
            catch(std::runtime_error e)
            {
                throw std::runtime_error(e.what());
            }
        }
    }
    catch(std::exception& e)
    {
        std::cout << "[ERROR]  " << e.what() << std::endl;
        exit(-1);
    }

    return 0;
}
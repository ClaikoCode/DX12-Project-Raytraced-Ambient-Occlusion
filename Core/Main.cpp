#include <stdexcept>

#include "Window.h"
#include "App.h"

WCHAR WindowClass[Core::MAX_CLASS_NAME_LENGTH] = L"Windows application";
WCHAR WindowTitle[Core::MAX_CLASS_NAME_LENGTH] = L"DirectX12";


#define USE_720P 1280, 720
#define USE_1080P 1920, 1080
#define USE_1440P 2560, 1440
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	Core::Window window(WindowClass, WindowTitle, USE_1080P);

	window.Show();

	try 
	{
		RunApp(window);
	}
	catch (const std::exception& e)
	{
		// Print to console AND create a message box.
		OutputDebugStringA(e.what());
		MessageBoxA(nullptr, e.what(), "Error", MB_ICONERROR | MB_SETFOREGROUND);

		return 1;
	}

	return 0;
}
#include "Window.h"

// Gets the HINSTANCE of the current module
#define HInstance() GetModuleHandle(NULL)

Core::Window::Window(WCHAR* windowClass, WCHAR* windowTitle, UINT width, UINT height)
	: m_hWnd(nullptr), m_wcex(), m_width(width), m_height(height), m_msg()
{
	// Check string length
	if (wcslen(windowClass) > MAX_CLASS_NAME_LENGTH)
	{
		MessageBox(nullptr, L"Window class name is too long", L"Error", MB_OK);
		return;
	}

	if (wcslen(windowTitle) > MAX_CLASS_NAME_LENGTH)
	{
		MessageBox(nullptr, L"Window title is too long", L"Error", MB_OK);
		return;
	}

	// Define window class core
	m_wcex.cbSize = sizeof(WNDCLASSEX);
	m_wcex.style = CS_HREDRAW | CS_VREDRAW;
	m_wcex.cbClsExtra = 0;
	m_wcex.cbWndExtra = 0;

	// Set window class visual properties
	m_wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	m_wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);

	// Set window class icons
	m_wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	m_wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

	// Set window name properties
	m_wcex.lpszClassName = windowClass;
	m_wcex.lpszMenuName = nullptr;

	// Set window class instance
	m_wcex.hInstance = HInstance();

	// Set window class message handler
	m_wcex.lpfnWndProc = Window::WindowProc;

	// Register window class
	RegisterClassEx(&m_wcex);

	m_hWnd = CreateWindow(windowClass, windowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, m_width, m_height, nullptr, nullptr, HInstance(), nullptr);

	if (!m_hWnd)
	{
		MessageBox(nullptr, L"Failed to create window", L"Error", MB_OK);
		return;
	}
}

Core::Window::~Window()
{
	DestroyWindow(m_hWnd);
	UnregisterClass(m_wcex.lpszClassName, HInstance());
}

void Core::Window::Show()
{
	ShowWindow(m_hWnd, SW_SHOW);
}

bool Core::Window::Closed()
{
	return m_msg.message == WM_QUIT;
}

void Core::Window::ProcessMessages()
{
	if (PeekMessage(&m_msg, 0, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&m_msg);
		DispatchMessage(&m_msg);
	}
}

LRESULT CALLBACK Core::Window::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_DESTROY:
		{
			PostQuitMessage(0);
			break;
		}
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}



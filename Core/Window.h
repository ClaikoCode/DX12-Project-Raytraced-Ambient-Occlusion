#pragma once

#include <Windows.h>

namespace Core
{
	constexpr UINT MAX_CLASS_NAME_LENGTH = 256u;

	class Window
	{
	public:
		Window(WCHAR* windowClass, WCHAR* windowTitle, UINT width, UINT height);
		~Window();

		void Show();
		bool Closed();

		void ProcessMessages();

		UINT Width() const { return m_width; }
		UINT Height() const { return m_height; }
		HWND Handle() const { return m_hWnd; }

	private:

		static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	private:
		HWND m_hWnd;
		WNDCLASSEX m_wcex;

		UINT m_width;
		UINT m_height;

		MSG m_msg;
	};
}
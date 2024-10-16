#include "GraphicsErrorHandling.h"

#include <Windows.h>
#include <stdexcept>
#include <string>
#include <format>
#include <algorithm>

// Wstring conversion includes
#include <locale>
#include <codecvt>
#pragma warning(disable : 4996) // Disable warning for "deprecated" conversion.

#include "DX12Renderer.h"

namespace ErrorHandling
{
	HrCatcher::HrCatcher(HRTYPE _hr, std::source_location _loc /*= std::source_location::current()*/) noexcept
		: hr(_hr), loc(_loc) {}

	void operator>>(HrCatcher catcher, HrPasserTag)
	{
		if (FAILED(catcher.hr))
		{
			std::wstring errorDescriptionWide;
			{
				WCHAR* descriptionAlloc = nullptr;
				auto result = FormatMessage(
					FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
					nullptr,
					catcher.hr,
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(LPWSTR)&descriptionAlloc,
					0,
					nullptr
				);

				if (result == 0)
				{
					errorDescriptionWide = std::wstring(L"[FAILED TO FORMAT ERROR]");
				}
				else
				{
					errorDescriptionWide = std::wstring(descriptionAlloc);
					LocalFree(descriptionAlloc);
				}
			}

			// Remove end return carriage
			if (errorDescriptionWide.ends_with(L"\r\n"))
			{
				errorDescriptionWide.resize(errorDescriptionWide.size() - 2);
			}

			// Copy the wide string into a regular string.
			using convertType = std::codecvt_utf8<wchar_t>;
			std::wstring_convert<convertType, wchar_t> wstringConverter;
			std::string errorDescription = wstringConverter.to_bytes(errorDescriptionWide);

			// Replace new lines with spaces.
			std::replace(errorDescription.begin(), errorDescription.end(), '\n', ' ');

			// Remove all return carriages.
			auto newStringEnd = std::remove(errorDescription.begin(), errorDescription.end(), '\r');
			errorDescription.erase(newStringEnd, errorDescription.end());

			// Format the error string.
			std::string errorString = std::format("Graphics ERROR ({}): {}\t{} ({})\n", catcher.hr, errorDescription, catcher.loc.file_name(), catcher.loc.line());

			// NOTE: The code below is faulty. More research has to be done to see if any value can be gained from
			// getting info from the info queue and if so, how it is properly done.
			//Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue = DX12Renderer::GetInfoQueue();
			//auto a = infoQueue->GetNumStoredMessages();
			//while (infoQueue != nullptr && infoQueue->GetNumStoredMessages() > 0)
			//{
			//	errorString += "ERROR STRING NOTICED\n";
			//
			//	SIZE_T messageLength = 0;
			//	HRESULT hr = infoQueue->GetMessageW(0, NULL, &messageLength);
			//
			//	if (FAILED(hr))
			//	{
			//		OutputDebugStringW(L"COULD NOT GET MESSAGE FROM INFO QUEUE\n");
			//		break;
			//	}
			//
			//	D3D12_MESSAGE* message = (D3D12_MESSAGE*)malloc(messageLength);
			//	hr = infoQueue->GetMessageW(0, message, &messageLength);
			//	
			//	if (FAILED(hr))
			//	{
			//		free(message);
			//		break;
			//	}
			//
			//	free(message);
			//
			//	infoQueue->message
			//}

			throw std::runtime_error(errorString);
		}
	}
}



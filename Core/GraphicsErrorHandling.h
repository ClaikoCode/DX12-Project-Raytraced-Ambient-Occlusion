#pragma once

#include <source_location>

// This Error handling system is fully based on the same system that is used by ChiliTomatoNoodle in his shallow dive tutorial series for DX12.
// The video for the implementation can be found here: https://www.youtube.com/watch?v=volqcWZjRig

// The system was implemented to be able to pass and handle HR values in a way that focuses on readability of the actual DX12 calls instead of
// having to use SUCCEEDED/FAILED macros all the time or ThrowIfFailed functions at the start of every call.

// The intended pattern of usage is to firstly instantiate a global HrPasserTag object and then use it in conjunction with a binary operator.
// The binary operator expects an HR value on the left hand side and the HrPasserTag object on the right hand side.
// Because HrCatcher is a struct that has a constructor that takes an HR value, it will be implicitly converted to an HrCatcher object.
// The binary operator logic then checks the HR value and throws an exception if it failed.

namespace ErrorHandling
{
	typedef unsigned int HRTYPE;

	// This structure exists to be used on the right hand side of a binary operator. 
	struct HrPasserTag {};

	// Used to catch an HR value and the location in the source code of where it was caught.
	struct HrCatcher
	{
		HrCatcher(HRTYPE _hr, std::source_location _loc = std::source_location::current()) noexcept;

		HRTYPE hr;
		std::source_location loc;
	};

	// Expects an HR value on LHS and an instance of the hr passer tag on RHS.
	// The function then checks the HR value and throws an exception if it failed.
	void operator>>(HrCatcher catcher, HrPasserTag);
}

// This macro is to be used in all scenarios where an HR value is returned from a function.
static ErrorHandling::HrPasserTag HR_PASSER_TAG;
#define CHK_HR HR_PASSER_TAG
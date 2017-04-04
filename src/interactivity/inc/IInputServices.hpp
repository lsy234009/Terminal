/*++
Copyright (c) Microsoft Corporation

Module Name:
- IInputServices.hpp

Abstract:
- Defines the methods used by the console to process input.

Author(s):
- Hernan Gatta (HeGatta) 29-Mar-2017
--*/

#pragma once

namespace Microsoft
{
    namespace Console
    {
        namespace Interactivity
        {
            class IInputServices
            {
            public:
                 virtual UINT MapVirtualKeyW(_In_ UINT uCode, _In_ UINT uMapType) = 0;
                 virtual SHORT VkKeyScanW(_In_ WCHAR ch) = 0;
                 virtual SHORT GetKeyState(_In_ int nVirtKey) = 0;
                 virtual BOOL TranslateCharsetInfo(_Inout_ DWORD FAR *lpSrc, _Out_ LPCHARSETINFO lpCs, _In_ DWORD dwFlags) = 0;

            protected:
                IInputServices() { }

                IInputServices(IInputServices const&) = delete;
                IInputServices& operator=(IInputServices const&) = delete;
            };
        };
    };
};
